#include "request_handler.hpp"
#include "wss_listener.hpp"

#include <App.h> // websockets
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <lyra/lyra.hpp> // commandline parsing
#include <quill/Quill.h> // logging
#include <stdexcept>
#include <thread>

int main(int argc, char** argv)
{
    std::string certpath;
    uint16_t port{};
    uint16_t znnd_port{35998};
    uint16_t threads{10};
    uint16_t timeout{50};
    bool help{false};
    auto cli = lyra::cli() | lyra::opt(port, "port")["-p"]["--port"]("WS(S)-Port to listen on").required() |
               lyra::opt(znnd_port, "znnd-port")["-z"]["--znndport"](
                   "Port at which znnd is expecting websocket connections (ws). Default: 35998.") |
               lyra::opt(certpath, "certpath")["-c"]["--certpath"](
                   "Absolute path containing fullchain.pem and privkey.pem. If given, wss is used, else ws.") |
               lyra::opt(threads, "N threads")["-n"]["--nthreads"]("Amount of listener threads to start. Default: 10") |
               lyra::opt(timeout, "timeout")["-t"]["--timeout"](
                   "Timeout value for a single listener thread while waiting for the result from the Node") |
               lyra::help(help).description("Reverse proxy for a public Zenon Network Node");

    auto const result = cli.parse({argc, argv});
    if (!result)
    {
        std::cerr << "Commandline error: " << result.message() << std::endl;
        std::cout << cli << std::endl;
        return 0;
    }

    if (help)
    {
        std::cout << cli << std::endl;
        return 0;
    }

    std::filesystem::path certs{certpath};

    auto const keyfile = certs / "privkey.pem";
    auto const certfile = certs / "fullchain.pem";
    bool is_ssl, file_error{};

    try
    {
        is_ssl = !certpath.empty();
        file_error = !(std::filesystem::exists(certs) && std::filesystem::is_regular_file(keyfile) &&
                       std::filesystem::is_regular_file(certfile));
    }
    catch (std::filesystem::filesystem_error const& err)
    {
        std::cerr << err.what() << std::endl;
        file_error = true;
    }

    if (is_ssl && file_error)
    {
        std::cerr << "Invalid option  for '--certpath': " << certpath << std::endl;
        std::cout << cli << std::endl;
        return 0;
    }

    quill::enable_console_colours();
    quill::start();

    auto* logger{quill::get_logger()};
    logger->set_log_level(quill::LogLevel::TraceL3);

    LOG_INFO(logger, "Starting {} listener", is_ssl ? "secure" : "insecure");

    std::vector<std::thread> server_threads;
    if (is_ssl)
    {
        for (size_t i{}; i < threads; i++)
        {
            server_threads.emplace_back([&] {
                reverse::listener<uWS::SSLApp>(
                    "/*", port, timeout,
                    uWS::SSLApp({.key_file_name = keyfile.c_str(), .cert_file_name = certfile.c_str()}));
            });
        }
    }

    else
    {
        for (size_t i{}; i < threads; i++)
        {
            server_threads.emplace_back([&] { reverse::listener<uWS::App>("/*", port, timeout, uWS::App{}); });
        }
    }

    // We never reach this currently; the threaded listeners can't be stopped
    for (auto&& t : server_threads)
    {
        t.join();
    }
}
