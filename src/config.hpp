#pragma once

#include "quill/LogLevel.h"
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <pwd.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/types.h>
#include <unistd.h>
#include <unordered_map>

namespace reverse::config
{
    struct options
    {
        uint16_t threads;
        uint16_t port;
        uint16_t znn_ws_port;
        uint16_t timeout;
        bool ssl;
        std::string certpath;
        std::string loglevel;
    };

    auto operator<<(std::ostream& os, options const& opt) -> std::ostream&
    {
        os << "Threads=" << opt.threads << ",Port=" << opt.port << ",Node Port=" << opt.znn_ws_port
           << ", Timeout=" << opt.timeout << ", SSL=" << opt.ssl;
        if (opt.ssl) os << ", Certpath=" << opt.certpath;
        os << ", Loglevel=" << opt.loglevel;

        return os;
    }

    auto to_string(options const& opts)
    {
        std::ostringstream oss;
        oss << opts;
        return oss.str();
    }

    namespace detail
    {

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

        template <typename T> inline auto get_or_throw(nlohmann::json const& object, std::string_view key) -> T
        {
            if (object.contains(key))
            {
                return object.at(key.data()).get<T>(); // throws if wrong type
            }

            throw std::runtime_error{std::string("Key ") + key.data() + " missing"};
        }
    } // namespace detail

    inline auto read_config_file() -> options
    {
        std::ifstream ifs(detail::get_config_file());

        if (ifs.is_open())
        {
            auto const json = nlohmann::json::parse(ifs);
            auto const wss = detail::get_or_throw<bool>(json, "wss");

            return {.threads = detail::get_or_throw<uint16_t>(json, "threads"),
                    .port = detail::get_or_throw<uint16_t>(json, "listen_port"),
                    .znn_ws_port = detail::get_or_throw<uint16_t>(json, "znn_port"),
                    .timeout = detail::get_or_throw<uint16_t>(json, "timeout"),
                    .ssl = wss,
                    .certpath = wss ? detail::get_or_throw<std::string>(json, "certificates_path") : "",
                    .loglevel = detail::get_or_throw<std::string>(json, "loglevel")};
        }

        throw std::runtime_error{"Could not read configuration file"};
    }

    inline auto quill_log_level(options const& opts)
    {
        static const std::unordered_map<std::string, quill::LogLevel> mapping = {{"debug", quill::LogLevel::Debug},
                                                                                 {"info", quill::LogLevel::Info},
                                                                                 {"warning", quill::LogLevel::Warning},
                                                                                 {"error", quill::LogLevel::Error},
                                                                                 {"all", quill::LogLevel::TraceL3}};

        std::string normalized;
        std::transform(opts.loglevel.begin(), opts.loglevel.end(), std::back_inserter(normalized),
                       [](auto c) { return std::tolower(c); });

        return mapping.contains(normalized) ? mapping.at(normalized) : quill::LogLevel::Info;
    }
} // namespace reverse::config
