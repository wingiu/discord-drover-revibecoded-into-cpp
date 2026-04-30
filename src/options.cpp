#include "options.h"

void ProxyValue::parse_from_string(const std::string& url)
{
    *this = ProxyValue{};

    static const std::regex re(R"(^\s*(?:([a-z\d]+)://)?(?:(.+):(.+)@)?(.+):(\d+)\s*$)",
                               std::regex::icase);
    std::smatch match;
    if (!std::regex_match(url, match, re)) {
        return;
    }

    is_specified = true;
    prot = to_lower(trim(match[1].str()));
    if (prot.empty() || prot == "https") {
        prot = "http";
    }

    login = trim(match[2].str());
    password = trim(match[3].str());
    host = trim(match[4].str());
    port = std::atoi(match[5].str().c_str());

    is_http = prot == "http";
    is_socks5 = prot == "socks5";
    is_auth = !login.empty() && !password.empty();
}

std::string ProxyValue::format_to_http_env() const
{
    if (!is_specified) {
        return {};
    }

    std::string result = "http://";
    if (is_auth) {
        result += login + ":" + password + "@";
    }
    result += host + ":" + std::to_string(port);
    return result;
}

std::wstring ProxyValue::format_to_chrome_proxy() const
{
    if (!is_specified) {
        return {};
    }
    return utf8_to_wide(prot + "://" + host + ":" + std::to_string(port));
}

static std::string read_ini_string(const std::wstring& filename,
                                   const wchar_t* section,
                                   const wchar_t* key,
                                   const wchar_t* fallback)
{
    std::wstring buffer(4096, L'\0');
    DWORD len = GetPrivateProfileStringW(section,
                                         key,
                                         fallback,
                                         buffer.data(),
                                         static_cast<DWORD>(buffer.size()),
                                         filename.c_str());
    buffer.resize(len);
    return wide_to_utf8(buffer);
}

DroverOptions load_options(const std::wstring& filename)
{
    DroverOptions options;
    options.proxy = read_ini_string(filename, L"drover", L"proxy", L"");
    return options;
}
