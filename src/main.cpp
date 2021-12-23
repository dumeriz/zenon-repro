#include "config.hpp"
#include "proxy_fabric.hpp"
#include "quill/LogLevel.h"
#include "quill/detail/LogMacros.h"

#include <atomic>

#include <algorithm>
#include <csignal>
#include <exception>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>

#include <systemd/sd-daemon.h>
#include <systemd/sd-journal.h>

#include <quill/Quill.h> // logging

std::atomic_bool sigterm{};

auto systemd_signal(std::string_view state)
{
    bool unset_environment = false;
    (void)sd_notify(static_cast<int>(unset_environment), state.data());
}

auto sigterm_handler(int) -> void
{
    sigterm.store(true);
    systemd_signal("STOPPING=1");
}

auto sigint_handler(int) -> void { sigterm.store(true); }

// printing to cerr and systemd log
template <typename... Args> auto log_error(Args&&... args)
{
    std::ostringstream oss;
    (oss << ... << args);
    std::cerr << oss.str() << std::endl;
    sd_journal_print(LOG_ERR, "%s", oss.str().data());
}

template <typename... Args> auto log_debug(Args&&... args)
{
#ifndef NDEBUG
    (std::cout << ... << args);
#endif
}

template <typename... Args> auto log_and_signal_error(int errnum, Args&&... args)
{
    log_error(std::forward<Args>(args)...);
    systemd_signal("ERRNO=" + std::to_string(-errnum));
    return -errnum;
}

auto setup_logging(quill::LogLevel loglevel)
{
    quill::enable_console_colours();
    quill::start();

    auto* logger{quill::get_logger()};
    logger->set_log_level(loglevel);
}

auto check_for_certfiles(std::filesystem::path keyfile, std::filesystem::path certfile) -> std::optional<std::string>
{
    auto certpath = keyfile.parent_path();
    log_debug("Reading privkey and fullchain from %s", certpath);

    try
    {
        if (!(std::filesystem::exists(certpath) && std::filesystem::is_regular_file(keyfile) &&
              std::filesystem::is_regular_file(certfile)))
        {
            return std::make_optional<std::string>("Could not read certificates from " + certpath.string());
        }
    }
    catch (std::filesystem::filesystem_error const& err)
    {
        return std::make_optional<std::string>(err.what());
    }

    return std::nullopt;
}

auto run_until_signalled()
{
    while (!sigterm.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

int main(int argc, char** argv)
{
    reverse::config::options config;
    try
    {
        config = reverse::config::read_config_file();
    }
    catch (std::exception const& e)
    {
        return log_and_signal_error(1, "Error reading the configuration file from ",
                                    reverse::config::detail::get_config_file(), ":", e.what());
    }

    setup_logging(reverse::config::quill_log_level(config));
    log_debug(reverse::config::to_string(config));

    std::filesystem::path certs{config.certificates};

    auto const keyfile = certs / "privkey.pem";
    auto const certfile = certs / "fullchain.pem";

    if (reverse::config::any_wss(config))
    {
        if (auto certfile_error = check_for_certfiles(keyfile, certfile); certfile_error)
        {
            return log_and_signal_error(2, "SSL-configuration failure: ", certfile_error.value());
        }
    }

    std::signal(SIGTERM, sigterm_handler); // signal send by systemd
    std::signal(SIGINT, sigterm_handler);  // when run manually, catch ctrl-c

    reverse::proxy_fabric fabric;
    std::vector<std::pair<bool, size_t>> start_results;

    for (auto&& proxy : config.proxies)
    {
        auto const proto{proxy.wss ? reverse::proto::wss : reverse::proto::ws};
        auto const node_url{reverse::config::node_url(proxy)};
        auto const node_port{reverse::config::node_port(proxy)};

        start_results.push_back(fabric.add_proxy(proto, {.public_port = proxy.port,
                                                         .znn_node_url = node_url,
                                                         .znn_node_port = node_port,
                                                         .timeout = proxy.timeout,
                                                         .keyfile = keyfile,
                                                         .certfile = certfile}));
    }

    if (std::all_of(start_results.begin(), start_results.end(), [](auto res) { return res.first; }))
    {
        systemd_signal("READY=1");
        run_until_signalled();
    }
    else
    {
        auto failed_proxies =
            std::accumulate(start_results.begin(), start_results.end(), std::string(), [](auto acc, auto res) {
                return acc + (res.first ? "" : "[" + std::to_string(res.second) + "]");
            });
        return log_and_signal_error(3, "Error starting proxies " + failed_proxies);
    }

    systemd_signal("STOPPING=1");
    fabric.close();
}
