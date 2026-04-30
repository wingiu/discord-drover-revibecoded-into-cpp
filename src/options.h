#pragma once

#include "common.h"

constexpr wchar_t DLL_FILENAME[] = L"version.dll";
constexpr wchar_t OPTIONS_FILENAME[] = L"drover.ini";

struct DroverOptions {
    std::string proxy;
};

struct ProxyValue {
    bool is_specified = false;
    std::string prot;
    std::string login;
    std::string password;
    std::string host;
    int port = 0;
    bool is_http = false;
    bool is_socks5 = false;
    bool is_auth = false;

    void parse_from_string(const std::string& url);
    std::string format_to_http_env() const;
    std::wstring format_to_chrome_proxy() const;
};

DroverOptions load_options(const std::wstring& filename);
