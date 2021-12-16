#include "config.hpp"
#include "quill/detail/LogMacros.h"
#include "request_handler.hpp"
#include "wss_listener.hpp"

#include <App.h> // websockets
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <quill/Quill.h> // logging
#include <stdexcept>
#include <thread>

int main(int argc, char** argv)
{
    proxy::config::options config;
    try
    {
        config = proxy::config::read_config_file();
    }
    catch (std::runtime_error const& e)
    {
        std::cerr << "Error reading the configuration file from " << proxy::config::get_config_file() << ":"
                  << std::endl
                  << e.what() << std::endl;
        return -1;
    }
    catch (nlohmann::json::parse_error const& pe)
    {
        std::cerr << "Error reading the configuration:" << std::endl << pe.what() << std::endl;
        return -1;
    }

    quill::enable_console_colours();
    quill::start();

    auto* logger{quill::get_logger()};
    logger->set_log_level(quill::LogLevel::TraceL3);
    std::filesystem::path certs{config.certpath};

    auto const keyfile = certs / "privkey.pem";
    auto const certfile = certs / "fullchain.pem";
    bool file_error{};

    if (config.ssl)
    {
        LOG_INFO(logger, "Reading privkey and fullchain from {}", certs);

        try
        {
            file_error = !(std::filesystem::exists(certs) && std::filesystem::is_regular_file(keyfile) &&
                           std::filesystem::is_regular_file(certfile));
        }
        catch (std::filesystem::filesystem_error const& err)
        {
            std::cerr << err.what() << std::endl;
            file_error = true;
        }
    }

    if (config.ssl && file_error)
    {
        std::cerr << "SSL-configuration failure" << std::endl;
        return 0;
    }

    LOG_INFO(logger, "Starting {} listener with {} threads on port {}", config.ssl ? "secure" : "insecure",
             config.threads, config.port);

    std::vector<std::thread> server_threads;
    if (config.ssl)
    {
        for (size_t i{1}; i <= config.threads; i++)
        {
            server_threads.emplace_back([&] {
                reverse::listener<uWS::SSLApp>(
                    "/*", config.port, config.znn_ws_port, config.timeout,
                    uWS::SSLApp({.key_file_name = keyfile.c_str(), .cert_file_name = certfile.c_str()}));
            });
        }
    }

    else
    {
        for (size_t i{1}; i <= config.threads; i++)
        {
            server_threads.emplace_back([&] {
                reverse::listener<uWS::App>("/*", config.port, config.znn_ws_port, config.timeout, uWS::App{});
            });
        }
    }

    // todo (necessary?): share a state variable with the listeners so that they can signal when they stop due to
    // errors.
    //                    iterate over these variables periodically and try to join/restart failed listeners.
    for (auto&& t : server_threads)
    {
        t.join();
    }

    // We never reach this currently; the threaded listeners can't be stopped
}
