// websockets_proxy.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <signal.h>
#include <algorithm>
#include <cctype>
#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/sinks/basic_file_sink.h>
#ifdef DEBUG
#include <spdlog/sinks/stdout_color_sinks.h>
#endif
#include "alpaca_websocket_proxy.h"
#include <alpaca_websocket_proxy/version.h>

#ifdef _WIN32
#pragma comment(lib, "Ws2_32.lib")
#endif

using namespace alpaca_websocket_proxy;

void atexit_hanlder()
{
    spdlog::shutdown();
}

/**
* Usage:
* WebsocketsProxy.exe [-s <server_queue_size>] [-l <logging_level>]
* 
* Arguments:
*   -s [optional]: Specify server to client queue size in Byte. Default to 16777216 Bytes.
*   -l [optional]: Specify logging level. By default, logging will be disabled in release build.
*                  Valid Logging level: OFF, CRITICAL, ERROR, WARNING, INFO, DEBUG, TRACE 
*/
int main(int argc, char* argv[])
{
    std::atexit(atexit_hanlder);
    spdlog::init_thread_pool(8192, 1);
#ifdef DEBUG
    auto stdout_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("./log/AlpacaWebsocketProxy.log");
    std::vector<spdlog::sink_ptr> sinks {stdout_sink, file_sink};
    auto logger = std::make_shared<spdlog::async_logger>("websocket_proxy_logger", sinks.begin(), sinks.end(), spdlog::thread_pool(), spdlog::async_overflow_policy::overrun_oldest);
    spdlog::set_default_logger(logger);
    spdlog::flush_on(spdlog::level::trace);
#else
    auto async_file_logger = spdlog::basic_logger_mt<spdlog::async_factory>("async_file_logger", "./log/AlpacaWebsocketProxy.log");
    spdlog::set_default_logger(async_file_logger);
    spdlog::flush_on(spdlog::level::info);
#endif

    uint32_t server_queue_size = 1 << 24;   // 16MB
    [[maybe_unused]] bool log_level_set = false;
    for (int i = 1; i < argc - 1; ++i) {
        if (_stricmp(argv[i], "-l") == 0) {
            std::string l = argv[++i];
            std::transform(l.begin(), l.end(), l.begin(), [](char c){ return std::tolower(c); });
            log_level_set = true;
            if (l == "off") {
                spdlog::set_level(spdlog::level::off);
            }
            else if (l == "critical") {
                spdlog::set_level(spdlog::level::critical);
                spdlog::flush_on(spdlog::level::critical);
            }
            else if (l == "error") {
                spdlog::set_level(spdlog::level::err);
                spdlog::flush_on(spdlog::level::err);
            }
            else if (l == "warning") {
                spdlog::set_level(spdlog::level::warn);
                spdlog::flush_on(spdlog::level::warn);
            }
            else if (l == "info") {
                spdlog::set_level(spdlog::level::info);
            }
            else if (l == "debug") {
                spdlog::set_level(spdlog::level::debug);
            }
            else if (l == "trace") {
                spdlog::set_level(spdlog::level::trace);
            }
            else {
                log_level_set = false;
            }
        }
        else if (_stricmp(argv[i], "-s") == 0) {
            server_queue_size = atoi(argv[++i]);
        }
    }

#ifdef DEBUG
    if (!log_level_set) {
        spdlog::set_level(spdlog::level::trace);
    }
#endif

    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%F][%t][%l][%s:%#] %v");

    SPDLOG_INFO(std::format("Start AlpacaWebsocketProxy {}.{}.{}.{} ...", AlpacaWebsocketProxy_VERSION_MAJOR, AlpacaWebsocketProxy_VERSION_MINOR, AlpacaWebsocketProxy_VERSION_PATCH, AlpacaWebsocketProxy_VERSION_TWEAK));
    AlpacaWebsocketProxy proxy(server_queue_size);
    proxy.run();
    SPDLOG_INFO("AlpacaWebsocketProxy Exit.");
}