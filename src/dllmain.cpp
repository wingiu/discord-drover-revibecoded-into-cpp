#include "common.h"
#include "discord_folders.h"
#include "options.h"
#include "socket_manager.h"

#include <MinHook.h>
#include <intrin.h>

using GetEnvironmentVariableWFn = DWORD(WINAPI*)(LPCWSTR, LPWSTR, DWORD);
using GetEnvironmentVariableAFn = DWORD(WINAPI*)(LPCSTR, LPSTR, DWORD);
using CreateProcessWFn = BOOL(WINAPI*)(LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID, LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION);
using GetCommandLineWFn = LPWSTR(WINAPI*)();
using GetProcAddressFn = FARPROC(WINAPI*)(HMODULE, LPCSTR);
using LoadLibraryAFn = HMODULE(WINAPI*)(LPCSTR);
using LoadLibraryWFn = HMODULE(WINAPI*)(LPCWSTR);
using LoadLibraryExAFn = HMODULE(WINAPI*)(LPCSTR, HANDLE, DWORD);
using LoadLibraryExWFn = HMODULE(WINAPI*)(LPCWSTR, HANDLE, DWORD);
using SocketFn = SOCKET(WINAPI*)(int, int, int);
using WSASocketWFn = SOCKET(WINAPI*)(int, int, int, LPWSAPROTOCOL_INFOW, GROUP, DWORD);
using WSASocketAFn = SOCKET(WINAPI*)(int, int, int, LPWSAPROTOCOL_INFOA, GROUP, DWORD);
using WSASendFn = int(WINAPI*)(SOCKET, LPWSABUF, DWORD, LPDWORD, DWORD, LPWSAOVERLAPPED, LPWSAOVERLAPPED_COMPLETION_ROUTINE);
using WSASendToFn = int(WINAPI*)(SOCKET, LPWSABUF, DWORD, LPDWORD, DWORD, const sockaddr*, int, LPWSAOVERLAPPED, LPWSAOVERLAPPED_COMPLETION_ROUTINE);
using SendFn = int(WINAPI*)(SOCKET, const char*, int, int);
using RecvFn = int(WINAPI*)(SOCKET, char*, int, int);

static GetEnvironmentVariableWFn real_get_environment_variable_w = GetEnvironmentVariableW;
static GetEnvironmentVariableAFn real_get_environment_variable_a = GetEnvironmentVariableA;
static CreateProcessWFn real_create_process_w = CreateProcessW;
static GetCommandLineWFn real_get_command_line_w = GetCommandLineW;
static SocketFn real_socket = socket;
static WSASocketWFn real_wsa_socket_w = WSASocketW;
static WSASocketAFn real_wsa_socket_a = WSASocketA;
static WSASendFn real_wsa_send = WSASend;
static WSASendToFn real_wsa_send_to = WSASendTo;
static SendFn real_send = send;
static RecvFn real_recv = recv;

static SocketManager socket_manager;
static ProxyValue proxy_value;
static std::wstring process_dir;
static thread_local bool in_hook = false;
static std::once_flag fake_payload_once;
static std::vector<char> fake_payload_cache;

static const std::vector<char>& fake_payload();
static bool is_current_process_discord();

struct PebUnicodeString {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR Buffer;
};

struct PebProcessParameters {
    BYTE Reserved1[16];
    PVOID Reserved2[10];
    PebUnicodeString ImagePathName;
    PebUnicodeString CommandLine;
};

struct Peb {
    BYTE Reserved1[2];
    BYTE BeingDebugged;
    BYTE Reserved2[1];
    PVOID Reserved3[2];
    PVOID Ldr;
    PebProcessParameters* ProcessParameters;
};

static Peb* current_peb()
{
#if defined(_M_X64) || defined(__x86_64__)
    return reinterpret_cast<Peb*>(__readgsqword(0x60));
#elif defined(_M_IX86) || defined(__i386__)
    return reinterpret_cast<Peb*>(__readfsdword(0x30));
#else
    return nullptr;
#endif
}

static void find_discord_dirs(std::vector<std::wstring>& dirs)
{
    std::filesystem::path base = std::filesystem::path(process_dir);
    if (!std::filesystem::exists(base / L"Update.exe")) {
        base = base.parent_path();
    }
    if (!std::filesystem::exists(base)) {
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(base)) {
        if (!entry.is_directory()) {
            continue;
        }
        const auto filename = entry.path().filename().wstring();
        if (filename.rfind(L"app-", 0) == 0) {
            auto dir = entry.path().wstring() + L"\\";
            if (dir_has_discord_executable(dir)) {
                dirs.push_back(dir);
            }
        }
    }
}

static void copy_files_to_all_discord_dirs()
{
    const auto src_options = std::filesystem::path(process_dir) / OPTIONS_FILENAME;
    const auto src_dll = std::filesystem::path(process_dir) / DLL_FILENAME;
    const auto src_fake = std::filesystem::path(process_dir) / L"fake.bin";
    if (!std::filesystem::exists(src_options) || !std::filesystem::exists(src_dll)) {
        return;
    }

    std::vector<std::wstring> dirs;
    find_discord_dirs(dirs);
    for (const auto& dir : dirs) {
      const auto dst_options = std::filesystem::path(dir) / OPTIONS_FILENAME;
      const auto dst_dll = std::filesystem::path(dir) / DLL_FILENAME;
      const auto dst_fake = std::filesystem::path(dir) / L"fake.bin";
      if (dir_has_discord_executable(dir) && !std::filesystem::exists(dst_options) && !std::filesystem::exists(dst_dll)) {
          CopyFileW(src_options.c_str(), dst_options.c_str(), TRUE);
          CopyFileW(src_dll.c_str(), dst_dll.c_str(), TRUE);
          if (std::filesystem::exists(src_fake) && !std::filesystem::exists(dst_fake)) {
              CopyFileW(src_fake.c_str(), dst_fake.c_str(), TRUE);
          }
      }
    }
}

static std::wstring launched_process_filename(LPCWSTR application_name, LPCWSTR command_line)
{
    if (application_name && *application_name) {
        return std::filesystem::path(application_name).filename().wstring();
    }

    if (!command_line || !*command_line) {
        return {};
    }

    std::wstring cmd = command_line;
    if (!cmd.empty() && cmd[0] == L'"') {
        const size_t end = cmd.find(L'"', 1);
        if (end != std::wstring::npos) {
            return std::filesystem::path(cmd.substr(1, end - 1)).filename().wstring();
        }
    }

    const size_t end = cmd.find(L' ');
    return std::filesystem::path(cmd.substr(0, end)).filename().wstring();
}

static void copy_files_on_create_process_if_needed(LPCWSTR application_name, LPCWSTR command_line)
{
    const auto filename = launched_process_filename(application_name, command_line);
    const auto lower = to_lower(filename);
    if (is_discord_executable(filename) || lower == L"reg.exe" || lower == L"update.exe") {
        copy_files_to_all_discord_dirs();
    }
}

static std::wstring command_line_with_proxy(LPCWSTR application_name, LPCWSTR command_line)
{
    if (!proxy_value.is_specified) {
        return {};
    }

    const auto filename = launched_process_filename(application_name, command_line);
    if (!is_discord_executable(filename)) {
        return {};
    }

    std::wstring updated = command_line ? command_line : L"";
    if (updated.empty() && application_name && *application_name) {
        updated = L"\"";
        updated += application_name;
        updated += L"\"";
    }
    if (updated.find(L" --proxy-server=") == std::wstring::npos) {
        updated += L" --proxy-server=" + proxy_value.format_to_chrome_proxy();
    }
    return updated;
}

extern "C" DWORD WINAPI hook_get_environment_variable_w(LPCWSTR name, LPWSTR buffer, DWORD size)
{
    if (proxy_value.is_specified && name) {
        const std::wstring lowered = to_lower(name);
        if (lowered.find(L"http_proxy") != std::wstring::npos || lowered.find(L"https_proxy") != std::wstring::npos) {
            const std::wstring value = utf8_to_wide(proxy_value.format_to_http_env());
            if (buffer && size > 0) {
                wcsncpy_s(buffer, size, value.c_str(), _TRUNCATE);
            }
            return static_cast<DWORD>(value.size());
        }
    }
    return real_get_environment_variable_w(name, buffer, size);
}

extern "C" DWORD WINAPI hook_get_environment_variable_a(LPCSTR name, LPSTR buffer, DWORD size)
{
    if (proxy_value.is_specified && name) {
        const std::string lowered = to_lower(std::string(name));
        if (lowered.find("http_proxy") != std::string::npos || lowered.find("https_proxy") != std::string::npos) {
            const std::string value = proxy_value.format_to_http_env();
            if (buffer && size > 0) {
                strncpy_s(buffer, size, value.c_str(), _TRUNCATE);
            }
            return static_cast<DWORD>(value.size());
        }
    }
    return real_get_environment_variable_a(name, buffer, size);
}

extern "C" BOOL WINAPI hook_create_process_w(LPCWSTR application_name,
                                             LPWSTR command_line,
                                             LPSECURITY_ATTRIBUTES process_attributes,
                                             LPSECURITY_ATTRIBUTES thread_attributes,
                                             BOOL inherit_handles,
                                             DWORD creation_flags,
                                             LPVOID environment,
                                             LPCWSTR current_directory,
                                             LPSTARTUPINFOW startup_info,
                                             LPPROCESS_INFORMATION process_information)
{
    if (!in_hook) {
        in_hook = true;
        copy_files_on_create_process_if_needed(application_name, command_line);
        in_hook = false;
    }

    std::wstring patched_command_line = command_line_with_proxy(application_name, command_line);
    LPWSTR effective_command_line = patched_command_line.empty() ? command_line : patched_command_line.data();

    return real_create_process_w(application_name,
                                 effective_command_line,
                                 process_attributes,
                                 thread_attributes,
                                 inherit_handles,
                                 creation_flags,
                                 environment,
                                 current_directory,
                                 startup_info,
                                 process_information);
}

extern "C" LPWSTR WINAPI hook_get_command_line_w()
{
    static std::wstring cached;
    LPWSTR original = real_get_command_line_w();
    cached = original ? original : L"";

    if (proxy_value.is_specified) {
        std::wstring exe(MAX_PATH, L'\0');
        DWORD len = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
        exe.resize(len);
        if (is_discord_executable(std::filesystem::path(exe).filename().wstring())
            && cached.find(L" --proxy-server=") == std::wstring::npos) {
            cached += L" --proxy-server=" + proxy_value.format_to_chrome_proxy();
        }
    }
    return cached.data();
}

extern "C" SOCKET WINAPI hook_socket(int af, int type, int protocol)
{
    SOCKET result = real_socket(af, type, protocol);
    socket_manager.add(result, type, protocol);
    return result;
}

extern "C" SOCKET WINAPI hook_wsa_socket_w(int af, int type, int protocol, LPWSAPROTOCOL_INFOW protocol_info, GROUP group, DWORD flags)
{
    SOCKET result = real_wsa_socket_w(af, type, protocol, protocol_info, group, flags);
    socket_manager.add(result, type, protocol);
    return result;
}

extern "C" SOCKET WINAPI hook_wsa_socket_a(int af, int type, int protocol, LPWSAPROTOCOL_INFOA protocol_info, GROUP group, DWORD flags)
{
    SOCKET result = real_wsa_socket_a(af, type, protocol, protocol_info, group, flags);
    socket_manager.add(result, type, protocol);
    return result;
}

static std::string base64_encode(const std::string& input)
{
    static constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0;
    int bits = -6;
    for (unsigned char ch : input) {
        val = (val << 8) + ch;
        bits += 8;
        while (bits >= 0) {
            out.push_back(table[(val >> bits) & 0x3F]);
            bits -= 6;
        }
    }
    if (bits > -6) {
        out.push_back(table[((val << 8) >> (bits + 8)) & 0x3F]);
    }
    while (out.size() % 4) {
        out.push_back('=');
    }
    return out;
}

static bool add_http_proxy_authorization_header(const SocketManagerItem& item, LPWSABUF buffers, DWORD buffer_count)
{
    if (!proxy_value.is_specified || !proxy_value.is_http || !proxy_value.is_auth || !item.is_tcp) {
        return false;
    }
    if (buffer_count != 1 || !buffers || buffers->len < 1 || !buffers->buf) {
        return false;
    }

    std::string packet(buffers->buf, buffers->buf + buffers->len);
    if (packet.find("\r\nProxy-Authorization: ") != std::string::npos) {
        return false;
    }

    const size_t ua_start = packet.find("User-Agent:");
    if (ua_start == std::string::npos) {
        return false;
    }
    const size_t ua_end = packet.find("\r\n", ua_start);
    if (ua_end == std::string::npos) {
        return false;
    }

    const size_t ua_len = ua_end - ua_start;
    std::string injected = "Proxy-Authorization: Basic " + base64_encode(proxy_value.login + ":" + proxy_value.password);
    const ptrdiff_t filler_len = static_cast<ptrdiff_t>(ua_len) - static_cast<ptrdiff_t>(injected.size());
    if (filler_len < 6) {
        return false;
    }

    injected += "\r\nX: " + std::string(static_cast<size_t>(filler_len - 5), 'X');
    if (injected.size() != ua_len) {
        return false;
    }

    std::copy(injected.begin(), injected.end(), buffers->buf + ua_start);
    return true;
}

extern "C" int WINAPI hook_wsa_send(SOCKET sock,
                                    LPWSABUF buffers,
                                    DWORD buffer_count,
                                    LPDWORD bytes_sent,
                                    DWORD flags,
                                    LPWSAOVERLAPPED overlapped,
                                    LPWSAOVERLAPPED_COMPLETION_ROUTINE completion_routine)
{
    SocketManagerItem item;
    if (socket_manager.is_first_send(sock, item)) {
        add_http_proxy_authorization_header(item, buffers, buffer_count);
    }
    return real_wsa_send(sock, buffers, buffer_count, bytes_sent, flags, overlapped, completion_routine);
}

extern "C" int WINAPI hook_wsa_send_to(SOCKET sock,
                                       LPWSABUF buffers,
                                       DWORD buffer_count,
                                       LPDWORD bytes_sent,
                                       DWORD flags,
                                       const sockaddr* to,
                                       int to_len,
                                       LPWSAOVERLAPPED overlapped,
                                       LPWSAOVERLAPPED_COMPLETION_ROUTINE completion_routine)
{
    SocketManagerItem item;
    if (socket_manager.is_first_send(sock, item)) {
        if (item.is_udp && buffers && buffers->len == 74) {
            const auto& payload = fake_payload();
            if (!payload.empty()) {
                sendto(sock, payload.data(), static_cast<int>(payload.size()), 0, to, to_len);
                Sleep(50);
            }
        }
    }
    return real_wsa_send_to(sock, buffers, buffer_count, bytes_sent, flags, to, to_len, overlapped, completion_routine);
}

static bool convert_http_to_socks5(const SocketManagerItem& item, const char* buf, int len, int flags)
{
    if (!proxy_value.is_specified || !proxy_value.is_socks5 || !item.is_tcp || !buf || len < 8) {
        return false;
    }
    if (std::string_view(buf, 8) != "CONNECT ") {
        return false;
    }

    const std::string request(buf, buf + len);
    static const std::regex re(R"(^CONNECT ([a-z\d.-]+):(\d+))", std::regex::icase);
    std::smatch match;
    if (!std::regex_search(request, match, re)) {
        return false;
    }

    const std::string target_host = match[1].str();
    const auto target_port = static_cast<unsigned short>(std::atoi(match[2].str().c_str()));
    const SOCKET sock = item.sock;

    const char greeting[] = {0x05, 0x01, 0x00};
    if (real_send(sock, greeting, sizeof(greeting), flags) != sizeof(greeting)) {
        return false;
    }

    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(sock, &read_set);
    timeval timeout{10, 0};
    if (select(0, &read_set, nullptr, nullptr, &timeout) < 1 || !FD_ISSET(sock, &read_set)) {
        return false;
    }

    char response[2] = {};
    if (real_recv(sock, response, sizeof(response), 0) != sizeof(response) || response[0] != 0x05 || response[1] != 0x00) {
        return false;
    }

    std::string connect;
    connect.push_back(0x05);
    connect.push_back(0x01);
    connect.push_back(0x00);
    connect.push_back(0x03);
    connect.push_back(static_cast<char>(target_host.size()));
    connect += target_host;
    connect.push_back(static_cast<char>((target_port >> 8) & 0xFF));
    connect.push_back(static_cast<char>(target_port & 0xFF));

    if (real_send(sock, connect.data(), static_cast<int>(connect.size()), flags) != static_cast<int>(connect.size())) {
        return false;
    }

    socket_manager.set_fake_http_proxy_flag(sock);
    return true;
}

static bool is_current_process_discord()
{
    std::wstring exe(MAX_PATH, L'\0');
    DWORD len = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
    if (len == 0) {
        return false;
    }
    exe.resize(len);
    return is_discord_executable(std::filesystem::path(exe).filename().wstring());
}

static void set_proxy_environment()
{
    if (!proxy_value.is_specified) {
        return;
    }

    const std::wstring value = utf8_to_wide(proxy_value.format_to_http_env());
    const std::wstring chrome_proxy = proxy_value.format_to_chrome_proxy();
    SetEnvironmentVariableW(L"http_proxy", value.c_str());
    SetEnvironmentVariableW(L"HTTP_PROXY", value.c_str());
    SetEnvironmentVariableW(L"https_proxy", value.c_str());
    SetEnvironmentVariableW(L"HTTPS_PROXY", value.c_str());
    SetEnvironmentVariableW(L"all_proxy", chrome_proxy.c_str());
    SetEnvironmentVariableW(L"ALL_PROXY", chrome_proxy.c_str());
}

static void append_proxy_to_process_command_line()
{
    if (!proxy_value.is_specified || !is_current_process_discord()) {
        return;
    }

    Peb* peb = current_peb();
    if (!peb || !peb->ProcessParameters || !peb->ProcessParameters->CommandLine.Buffer) {
        return;
    }

    auto& command_line = peb->ProcessParameters->CommandLine;
    std::wstring current(command_line.Buffer, command_line.Length / sizeof(wchar_t));
    if (current.find(L" --proxy-server=") != std::wstring::npos) {
        return;
    }

    current += L" --proxy-server=" + proxy_value.format_to_chrome_proxy();

    const size_t bytes = (current.size() + 1) * sizeof(wchar_t);
    auto* buffer = static_cast<PWSTR>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes));
    if (!buffer) {
        return;
    }

    memcpy(buffer, current.c_str(), bytes);
    command_line.Buffer = buffer;
    command_line.Length = static_cast<USHORT>((current.size()) * sizeof(wchar_t));
    command_line.MaximumLength = static_cast<USHORT>(bytes);
}

extern "C" int WINAPI hook_send(SOCKET sock, const char* buf, int len, int flags)
{
    SocketManagerItem item;
    if (socket_manager.is_first_send(sock, item)) {
        if (convert_http_to_socks5(item, buf, len, flags)) {
            return len;
        }
    }
    return real_send(sock, buf, len, flags);
}

extern "C" int WINAPI hook_recv(SOCKET sock, char* buf, int len, int flags)
{
    int result = real_recv(sock, buf, len, flags);
    if (result > 0 && socket_manager.reset_fake_http_proxy_flag(sock)) {
        if (result >= 10 && buf[0] == 0x05 && buf[1] == 0x00 && buf[2] == 0x00) {
            const char ok[] = "HTTP/1.1 200 Connection Established\r\n\r\n";
            const int ok_len = static_cast<int>(sizeof(ok) - 1);
            if (ok_len <= len) {
                memcpy(buf, ok, ok_len);
                return ok_len;
            }
        }
    }
    return result;
}

static void install_hooks()
{
    if (MH_Initialize() != MH_OK) {
        return;
    }

    auto hook_api = [](LPCWSTR module, LPCSTR name, void* detour, void** original) {
        const MH_STATUS status = MH_CreateHookApi(module, name, detour, original);
        return status == MH_OK || status == MH_ERROR_ALREADY_CREATED
            || status == MH_ERROR_MODULE_NOT_FOUND || status == MH_ERROR_FUNCTION_NOT_FOUND;
    };

    hook_api(L"kernel32.dll", "GetEnvironmentVariableW", reinterpret_cast<void*>(hook_get_environment_variable_w), reinterpret_cast<void**>(&real_get_environment_variable_w));
    hook_api(L"kernel32.dll", "GetEnvironmentVariableA", reinterpret_cast<void*>(hook_get_environment_variable_a), reinterpret_cast<void**>(&real_get_environment_variable_a));
    hook_api(L"kernel32.dll", "CreateProcessW", reinterpret_cast<void*>(hook_create_process_w), reinterpret_cast<void**>(&real_create_process_w));
    hook_api(L"kernel32.dll", "GetCommandLineW", reinterpret_cast<void*>(hook_get_command_line_w), reinterpret_cast<void**>(&real_get_command_line_w));

    hook_api(L"ws2_32.dll", "socket", reinterpret_cast<void*>(hook_socket), reinterpret_cast<void**>(&real_socket));
    hook_api(L"ws2_32.dll", "WSASocketW", reinterpret_cast<void*>(hook_wsa_socket_w), reinterpret_cast<void**>(&real_wsa_socket_w));
    hook_api(L"ws2_32.dll", "WSASocketA", reinterpret_cast<void*>(hook_wsa_socket_a), reinterpret_cast<void**>(&real_wsa_socket_a));
    hook_api(L"ws2_32.dll", "WSASend", reinterpret_cast<void*>(hook_wsa_send), reinterpret_cast<void**>(&real_wsa_send));
    hook_api(L"ws2_32.dll", "WSASendTo", reinterpret_cast<void*>(hook_wsa_send_to), reinterpret_cast<void**>(&real_wsa_send_to));
    hook_api(L"ws2_32.dll", "send", reinterpret_cast<void*>(hook_send), reinterpret_cast<void**>(&real_send));
    hook_api(L"ws2_32.dll", "recv", reinterpret_cast<void*>(hook_recv), reinterpret_cast<void**>(&real_recv));

    MH_EnableHook(MH_ALL_HOOKS);
}

static const std::vector<char>& fake_payload()
{
    std::call_once(fake_payload_once, [] {
        const auto path = std::filesystem::path(process_dir) / L"fake.bin";
        HANDLE file = CreateFileW(path.c_str(),
                                  GENERIC_READ,
                                  FILE_SHARE_READ,
                                  nullptr,
                                  OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL,
                                  nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            return;
        }

        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 1024 * 1024) {
            CloseHandle(file);
            return;
        }

        fake_payload_cache.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        if (!ReadFile(file,
                      fake_payload_cache.data(),
                      static_cast<DWORD>(fake_payload_cache.size()),
                      &read,
                      nullptr)
            || read != static_cast<DWORD>(fake_payload_cache.size())) {
            fake_payload_cache.clear();
        }
        CloseHandle(file);
    });
    return fake_payload_cache;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        process_dir = current_process_dir();
        const DroverOptions options = load_options(std::filesystem::path(process_dir) / OPTIONS_FILENAME);
        proxy_value.parse_from_string(options.proxy);
        set_proxy_environment();
        append_proxy_to_process_command_line();
        install_hooks();
    }
    return TRUE;
}
