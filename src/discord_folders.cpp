#include "discord_folders.h"

bool is_discord_executable(const std::wstring& filename)
{
    const auto name = to_lower(std::filesystem::path(filename).filename().wstring());
    return name == L"discord.exe" || name == L"discordcanary.exe" || name == L"discordptb.exe";
}

bool dir_has_discord_executable(const std::wstring& dir)
{
    const std::filesystem::path base(dir);
    return std::filesystem::exists(base / L"Discord.exe")
        || std::filesystem::exists(base / L"DiscordCanary.exe")
        || std::filesystem::exists(base / L"DiscordPTB.exe");
}
