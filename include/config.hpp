#pragma once

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
#include <pwd.h>
#include <stdexcept>
#include <string>
#include <sys/types.h>
#include <unistd.h>

namespace proxy::config
{
    struct options
    {
        uint16_t threads;
        uint16_t port;
        uint16_t znn_ws_port;
        uint16_t timeout;
        bool ssl;
        std::string certpath;
    };

    inline auto get_home_folder() -> std::filesystem::path
    {
        if (auto const* home = getenv("HOME"); home)
        {
            return home;
        }
        return getpwuid(getuid())->pw_dir;
    }

    inline auto get_config_folder() -> std::filesystem::path
    {
        return std::filesystem::path(get_home_folder()) / ".config" / "znn-repro";
    }

    inline auto get_config_file() -> std::filesystem::path { return get_config_folder() / "config.json"; }

    inline auto read_config_file() -> options
    {
        std::ifstream ifs(get_config_file());

        if (ifs.is_open())
        {
            auto const json = nlohmann::json::parse(ifs);
            auto const wss = json["wss"];

            return {.threads = json["threads"],
                    .port = json["listen_port"],
                    .znn_ws_port = json["znn_port"],
                    .timeout = json["timeout"],
                    .ssl = wss,
                    .certpath = wss ? json["certificates_path"] : ""};
        }

        throw std::runtime_error{"Could not read configuration file"};
    }
} // namespace proxy::config
