#include "common.h"
#include "discord_folders.h"
#include "options.h"

#include <set>
#include <tlhelp32.h>
#include <iostream>

static bool delete_if_exists(const std::filesystem::path& path);
static bool remove_update_folder_hook_files(bool remove_ini);

static std::wstring exe_dir()
{
    std::wstring path(MAX_PATH, L'\0');
    DWORD len = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    while (len == path.size()) {
        path.resize(path.size() * 2);
        len = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    }
    path.resize(len);
    return std::filesystem::path(path).parent_path().wstring() + L"\\";
}

static void add_unique_dir(std::vector<std::wstring>& dirs, const std::wstring& dir)
{
    if (dir.empty() || !std::filesystem::exists(dir)) {
        return;
    }

    std::wstring normalized = std::filesystem::path(dir).wstring();
    if (!normalized.empty() && normalized.back() != L'\\') {
        normalized.push_back(L'\\');
    }

    const auto lowered = to_lower(normalized);
    const auto exists = std::any_of(dirs.begin(), dirs.end(), [&](const std::wstring& value) {
        return to_lower(value) == lowered;
    });
    if (!exists) {
        dirs.push_back(normalized);
    }
}

static std::optional<std::wstring> read_reg_string(HKEY root, const wchar_t* subkey, const wchar_t* value_name)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subkey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return std::nullopt;
    }

    DWORD type = 0;
    DWORD bytes = 0;
    LSTATUS status = RegQueryValueExW(key, value_name, nullptr, &type, nullptr, &bytes);
    if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || bytes == 0) {
        RegCloseKey(key);
        return std::nullopt;
    }

    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    status = RegQueryValueExW(key,
                              value_name,
                              nullptr,
                              nullptr,
                              reinterpret_cast<LPBYTE>(value.data()),
                              &bytes);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) {
        return std::nullopt;
    }

    while (!value.empty() && value.back() == L'\0') {
        value.pop_back();
    }

    if (type == REG_EXPAND_SZ) {
        std::wstring expanded(32768, L'\0');
        DWORD len = ExpandEnvironmentStringsW(value.c_str(), expanded.data(), static_cast<DWORD>(expanded.size()));
        if (len > 0 && len < expanded.size()) {
            expanded.resize(len - 1);
            return expanded;
        }
    }

    return value;
}

static void find_discord_base_dirs(std::vector<std::wstring>& dirs)
{
    const wchar_t* apps[] = {L"Discord", L"DiscordCanary", L"DiscordPTB"};
    for (const wchar_t* app : apps) {
        std::wstring subkey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\";
        subkey += app;
        if (auto value = read_reg_string(HKEY_CURRENT_USER, subkey.c_str(), L"InstallLocation")) {
            add_unique_dir(dirs, *value);
        }
    }

    if (auto value = read_reg_string(HKEY_CURRENT_USER, L"Software\\Classes\\Discord\\shell\\open\\command", L"")) {
        const std::wregex re(LR"(^"(.+\\)app-)", std::regex::icase);
        std::wsmatch match;
        if (std::regex_search(*value, match, re)) {
            add_unique_dir(dirs, match[1].str());
        }
    }

    wchar_t local_app_data[MAX_PATH] = {};
    DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        const std::filesystem::path base(local_app_data);
        add_unique_dir(dirs, (base / L"Discord").wstring());
        add_unique_dir(dirs, (base / L"DiscordCanary").wstring());
        add_unique_dir(dirs, (base / L"DiscordPTB").wstring());
    }
}

static std::vector<std::wstring> find_discord_dirs()
{
    std::vector<std::wstring> base_dirs;
    find_discord_base_dirs(base_dirs);

    std::vector<std::wstring> dirs;
    for (const auto& base : base_dirs) {
        if (!std::filesystem::exists(base)) {
            continue;
        }

        for (const auto& entry : std::filesystem::directory_iterator(base)) {
            if (!entry.is_directory()) {
                continue;
            }

            const auto name = entry.path().filename().wstring();
            if (name.rfind(L"app-", 0) != 0) {
                continue;
            }

            std::wstring dir = entry.path().wstring() + L"\\";
            if (dir_has_discord_executable(dir)) {
                add_unique_dir(dirs, dir);
            }
        }
    }
    return dirs;
}

static bool is_discord_running()
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return false;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool running = false;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (is_discord_executable(entry.szExeFile)) {
                running = true;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return running;
}

static bool copy_if_exists(const std::filesystem::path& src, const std::filesystem::path& dst, bool required)
{
    if (!std::filesystem::exists(src)) {
        if (required) {
            std::wcerr << L"Missing required file: " << src.wstring() << L"\n";
        }
        return !required;
    }

    if (!CopyFileW(src.c_str(), dst.c_str(), FALSE)) {
        std::wcerr << L"Failed to copy " << src.filename().wstring() << L" to " << dst.wstring()
                   << L" (error " << GetLastError() << L")\n";
        return false;
    }

    std::wcout << L"Copied " << src.filename().wstring() << L" -> " << dst.wstring() << L"\n";
    return true;
}

static bool write_text_file(const std::filesystem::path& path, const std::string& text)
{
    HANDLE file = CreateFileW(path.c_str(),
                              GENERIC_WRITE,
                              0,
                              nullptr,
                              CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        std::wcerr << L"Failed to write " << path.wstring() << L" (error " << GetLastError() << L")\n";
        return false;
    }

    DWORD written = 0;
    const BOOL ok = WriteFile(file,
                              text.data(),
                              static_cast<DWORD>(text.size()),
                              &written,
                              nullptr);
    CloseHandle(file);

    if (!ok || written != text.size()) {
        std::wcerr << L"Failed to write " << path.wstring() << L" (error " << GetLastError() << L")\n";
        return false;
    }

    return true;
}

static std::optional<std::string> make_proxy_ini(int argc, wchar_t** argv)
{
    if (argc == 2) {
        return std::nullopt;
    }

    const std::wstring proxy_type = argc >= 3 ? to_lower(argv[2]) : L"";
    std::string proxy;

    if (proxy_type == L"direct") {
        if (argc != 3) {
            std::wcerr << L"Direct mode does not use ip:port.\n";
            return std::string{};
        }
    } else if (proxy_type == L"http" || proxy_type == L"socks5") {
        if (argc != 4) {
            std::wcerr << L"Proxy install requires: drover install " << proxy_type << L" ip:port\n";
            return std::string{};
        }

        const std::wstring endpoint_w = argv[3];
        const std::wregex endpoint_re(LR"(^([^:]+):(\d{1,5})$)");
        std::wsmatch match;
        if (!std::regex_match(endpoint_w, match, endpoint_re)) {
            std::wcerr << L"Invalid proxy endpoint. Use ip:port, for example 127.0.0.1:1080\n";
            return std::string{};
        }

        const int port = std::stoi(match[2].str());
        if (port < 1 || port > 65535) {
            std::wcerr << L"Invalid proxy port. Use 1..65535.\n";
            return std::string{};
        }

        proxy = wide_to_utf8(proxy_type + L"://" + endpoint_w);
    } else {
        std::wcerr << L"Invalid proxy type. Use http, socks5, or direct.\n";
        return std::string{};
    }

    return std::string("[drover]\nproxy = " + proxy + "\n");
}

static int install(const std::optional<std::string>& generated_ini)
{
    if (is_discord_running()) {
        std::wcerr << L"Close Discord before installing.\n";
        return 2;
    }

    const auto source_dir = std::filesystem::path(exe_dir());
    const auto dll_path = source_dir / DLL_FILENAME;
    const auto ini_path = source_dir / OPTIONS_FILENAME;
    const auto fake_path = source_dir / L"fake.bin";

    auto dirs = find_discord_dirs();
    if (dirs.empty()) {
        std::wcerr << L"Discord app-* folders were not found.\n";
        return 3;
    }

    bool ok = true;
    ok &= remove_update_folder_hook_files(false);
    if (generated_ini) {
        ok &= write_text_file(ini_path, *generated_ini);
    }

    for (const auto& dir : dirs) {
        const auto dst_dir = std::filesystem::path(dir);
        ok &= copy_if_exists(dll_path, dst_dir / DLL_FILENAME, true);
        if (generated_ini) {
            ok &= write_text_file(dst_dir / OPTIONS_FILENAME, *generated_ini);
            if (ok) {
                std::wcout << L"Written " << (dst_dir / OPTIONS_FILENAME).wstring() << L"\n";
            }
        } else {
            ok &= copy_if_exists(ini_path, dst_dir / OPTIONS_FILENAME, false);
        }
        ok &= copy_if_exists(fake_path, dst_dir / L"fake.bin", false);
    }

    if (ok) {
        std::wcout << L"Install complete. Edit drover.ini manually if needed.\n";
        return 0;
    }

    return 4;
}

static bool delete_if_exists(const std::filesystem::path& path)
{
    if (!std::filesystem::exists(path)) {
        return true;
    }

    if (!DeleteFileW(path.c_str())) {
        std::wcerr << L"Failed to delete " << path.wstring() << L" (error " << GetLastError() << L")\n";
        return false;
    }

    std::wcout << L"Deleted " << path.wstring() << L"\n";
    return true;
}

static bool remove_update_folder_hook_files(bool remove_ini)
{
    std::vector<std::wstring> base_dirs;
    find_discord_base_dirs(base_dirs);

    bool ok = true;
    for (const auto& dir : base_dirs) {
        const auto base = std::filesystem::path(dir);
        if (!std::filesystem::exists(base / L"Update.exe")) {
            continue;
        }

        ok &= delete_if_exists(base / DLL_FILENAME);
        ok &= delete_if_exists(base / L"fake.bin");
        if (remove_ini) {
            ok &= delete_if_exists(base / OPTIONS_FILENAME);
        }
    }
    return ok;
}

static int uninstall(bool remove_ini)
{
    if (is_discord_running()) {
        std::wcerr << L"Close Discord before uninstalling.\n";
        return 2;
    }

    auto dirs = find_discord_dirs();
    if (dirs.empty()) {
        std::wcerr << L"Discord app-* folders were not found.\n";
        return 3;
    }

    bool ok = remove_update_folder_hook_files(remove_ini);
    for (const auto& dir : dirs) {
        const auto dst_dir = std::filesystem::path(dir);
        ok &= delete_if_exists(dst_dir / DLL_FILENAME);
        ok &= delete_if_exists(dst_dir / L"fake.bin");
        if (remove_ini) {
            ok &= delete_if_exists(dst_dir / OPTIONS_FILENAME);
        }
    }

    if (ok) {
        std::wcout << L"Uninstall complete";
        if (!remove_ini) {
            std::wcout << L" (drover.ini kept)";
        }
        std::wcout << L".\n";
        return 0;
    }

    return 4;
}

static int list_dirs()
{
    auto dirs = find_discord_dirs();
    if (dirs.empty()) {
        std::wcerr << L"Discord app-* folders were not found.\n";
        return 3;
    }

    for (const auto& dir : dirs) {
        std::wcout << dir << L"\n";
    }
    return 0;
}

static void print_usage()
{
    std::wcout
        << L"Discord Drover console installer\n\n"
        << L"Usage:\n"
        << L"  drover install http ip:port\n"
        << L"  drover install socks5 ip:port\n"
        << L"  drover install direct\n"
        << L"  drover install\n"
        << L"  drover uninstall [--remove-ini]\n"
        << L"  drover list\n\n"
        << L"Examples:\n"
        << L"  drover install http 127.0.0.1:1080\n"
        << L"  drover install socks5 127.0.0.1:2080\n\n"
        << L"Place version.dll next to this installer before running install.\n"
        << L"fake.bin is copied too if it is next to the installer.\n"
        << L"Plain 'drover install' copies an existing drover.ini without editing it.\n";
}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2) {
        print_usage();
        return 1;
    }

    const std::wstring command = to_lower(argv[1]);
    if (command == L"install") {
        auto generated_ini = make_proxy_ini(argc, argv);
        if (argc > 2 && (!generated_ini || generated_ini->empty())) {
            return 1;
        }
        return install(generated_ini);
    }
    if (command == L"uninstall") {
        const bool remove_ini = argc >= 3 && to_lower(argv[2]) == L"--remove-ini";
        return uninstall(remove_ini);
    }
    if (command == L"list") {
        return list_dirs();
    }

    print_usage();
    return 1;
}
