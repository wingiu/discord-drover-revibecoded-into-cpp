#include "common.h"

#include <winver.h>

static HMODULE real_version_module = nullptr;

static HMODULE load_real_version_module()
{
    if (real_version_module) {
        return real_version_module;
    }

    wchar_t system_dir[MAX_PATH] = {};
    GetSystemDirectoryW(system_dir, MAX_PATH);
    std::filesystem::path path(system_dir);
    path /= L"version.dll";
    real_version_module = LoadLibraryW(path.c_str());
    return real_version_module;
}

template <typename Fn>
static Fn real_proc(const char* name)
{
    HMODULE module = load_real_version_module();
    return module ? reinterpret_cast<Fn>(GetProcAddress(module, name)) : nullptr;
}

extern "C" BOOL WINAPI GetFileVersionInfoA(LPCSTR filename, DWORD handle, DWORD len, LPVOID data)
{
    using Fn = BOOL(WINAPI*)(LPCSTR, DWORD, DWORD, LPVOID);
    if (auto fn = real_proc<Fn>("GetFileVersionInfoA")) {
        return fn(filename, handle, len, data);
    }
    return FALSE;
}

extern "C" BOOL WINAPI GetFileVersionInfoByHandle(DWORD handle, DWORD len, LPVOID data)
{
    using Fn = BOOL(WINAPI*)(DWORD, DWORD, LPVOID);
    if (auto fn = real_proc<Fn>("GetFileVersionInfoByHandle")) {
        return fn(handle, len, data);
    }
    return FALSE;
}

extern "C" BOOL WINAPI GetFileVersionInfoExA(DWORD flags, LPCSTR filename, DWORD handle, DWORD len, LPVOID data)
{
    using Fn = BOOL(WINAPI*)(DWORD, LPCSTR, DWORD, DWORD, LPVOID);
    if (auto fn = real_proc<Fn>("GetFileVersionInfoExA")) {
        return fn(flags, filename, handle, len, data);
    }
    return FALSE;
}

extern "C" BOOL WINAPI GetFileVersionInfoExW(DWORD flags, LPCWSTR filename, DWORD handle, DWORD len, LPVOID data)
{
    using Fn = BOOL(WINAPI*)(DWORD, LPCWSTR, DWORD, DWORD, LPVOID);
    if (auto fn = real_proc<Fn>("GetFileVersionInfoExW")) {
        return fn(flags, filename, handle, len, data);
    }
    return FALSE;
}

extern "C" DWORD WINAPI GetFileVersionInfoSizeA(LPCSTR filename, LPDWORD handle)
{
    using Fn = DWORD(WINAPI*)(LPCSTR, LPDWORD);
    if (auto fn = real_proc<Fn>("GetFileVersionInfoSizeA")) {
        return fn(filename, handle);
    }
    return 0;
}

extern "C" DWORD WINAPI GetFileVersionInfoSizeExA(DWORD flags, LPCSTR filename, LPDWORD handle)
{
    using Fn = DWORD(WINAPI*)(DWORD, LPCSTR, LPDWORD);
    if (auto fn = real_proc<Fn>("GetFileVersionInfoSizeExA")) {
        return fn(flags, filename, handle);
    }
    return 0;
}

extern "C" DWORD WINAPI GetFileVersionInfoSizeExW(DWORD flags, LPCWSTR filename, LPDWORD handle)
{
    using Fn = DWORD(WINAPI*)(DWORD, LPCWSTR, LPDWORD);
    if (auto fn = real_proc<Fn>("GetFileVersionInfoSizeExW")) {
        return fn(flags, filename, handle);
    }
    return 0;
}

extern "C" DWORD WINAPI GetFileVersionInfoSizeW(LPCWSTR filename, LPDWORD handle)
{
    using Fn = DWORD(WINAPI*)(LPCWSTR, LPDWORD);
    if (auto fn = real_proc<Fn>("GetFileVersionInfoSizeW")) {
        return fn(filename, handle);
    }
    return 0;
}

extern "C" BOOL WINAPI GetFileVersionInfoW(LPCWSTR filename, DWORD handle, DWORD len, LPVOID data)
{
    using Fn = BOOL(WINAPI*)(LPCWSTR, DWORD, DWORD, LPVOID);
    if (auto fn = real_proc<Fn>("GetFileVersionInfoW")) {
        return fn(filename, handle, len, data);
    }
    return FALSE;
}

extern "C" DWORD WINAPI VerFindFileA(DWORD flags, LPCSTR filename, LPCSTR win_dir, LPCSTR app_dir, LPSTR cur_dir, PUINT cur_dir_len, LPSTR dest_dir, PUINT dest_dir_len)
{
    using Fn = DWORD(WINAPI*)(DWORD, LPCSTR, LPCSTR, LPCSTR, LPSTR, PUINT, LPSTR, PUINT);
    if (auto fn = real_proc<Fn>("VerFindFileA")) {
        return fn(flags, filename, win_dir, app_dir, cur_dir, cur_dir_len, dest_dir, dest_dir_len);
    }
    return 0;
}

extern "C" DWORD WINAPI VerFindFileW(DWORD flags, LPCWSTR filename, LPCWSTR win_dir, LPCWSTR app_dir, LPWSTR cur_dir, PUINT cur_dir_len, LPWSTR dest_dir, PUINT dest_dir_len)
{
    using Fn = DWORD(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, PUINT, LPWSTR, PUINT);
    if (auto fn = real_proc<Fn>("VerFindFileW")) {
        return fn(flags, filename, win_dir, app_dir, cur_dir, cur_dir_len, dest_dir, dest_dir_len);
    }
    return 0;
}

extern "C" DWORD WINAPI VerInstallFileA(DWORD flags, LPCSTR src, LPCSTR dst, LPCSTR src_dir, LPCSTR dst_dir, LPCSTR cur_dir, LPSTR tmp, PUINT tmp_len)
{
    using Fn = DWORD(WINAPI*)(DWORD, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPSTR, PUINT);
    if (auto fn = real_proc<Fn>("VerInstallFileA")) {
        return fn(flags, src, dst, src_dir, dst_dir, cur_dir, tmp, tmp_len);
    }
    return 0;
}

extern "C" DWORD WINAPI VerInstallFileW(DWORD flags, LPCWSTR src, LPCWSTR dst, LPCWSTR src_dir, LPCWSTR dst_dir, LPCWSTR cur_dir, LPWSTR tmp, PUINT tmp_len)
{
    using Fn = DWORD(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, PUINT);
    if (auto fn = real_proc<Fn>("VerInstallFileW")) {
        return fn(flags, src, dst, src_dir, dst_dir, cur_dir, tmp, tmp_len);
    }
    return 0;
}

extern "C" DWORD WINAPI VerLanguageNameA(DWORD lang, LPSTR buffer, DWORD len)
{
    using Fn = DWORD(WINAPI*)(DWORD, LPSTR, DWORD);
    if (auto fn = real_proc<Fn>("VerLanguageNameA")) {
        return fn(lang, buffer, len);
    }
    return 0;
}

extern "C" DWORD WINAPI VerLanguageNameW(DWORD lang, LPWSTR buffer, DWORD len)
{
    using Fn = DWORD(WINAPI*)(DWORD, LPWSTR, DWORD);
    if (auto fn = real_proc<Fn>("VerLanguageNameW")) {
        return fn(lang, buffer, len);
    }
    return 0;
}

extern "C" BOOL WINAPI VerQueryValueA(LPCVOID block, LPCSTR sub_block, LPVOID* buffer, PUINT len)
{
    using Fn = BOOL(WINAPI*)(LPCVOID, LPCSTR, LPVOID*, PUINT);
    if (auto fn = real_proc<Fn>("VerQueryValueA")) {
        return fn(block, sub_block, buffer, len);
    }
    return FALSE;
}

extern "C" BOOL WINAPI VerQueryValueW(LPCVOID block, LPCWSTR sub_block, LPVOID* buffer, PUINT len)
{
    using Fn = BOOL(WINAPI*)(LPCVOID, LPCWSTR, LPVOID*, PUINT);
    if (auto fn = real_proc<Fn>("VerQueryValueW")) {
        return fn(block, sub_block, buffer, len);
    }
    return FALSE;
}
