// The MIT License (MIT) 
// Copyright (c) 2024-2025 Kun Zhao
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "pch.hpp"

#ifdef DEBUG
#include <spdlog/sinks/stdout_color_sinks.h>
#endif
#include "websocket_proxy.h"
#include <websocket_proxy/version.h>
#include <csignal>

#ifdef _WIN32
#pragma comment(lib, "Ws2_32.lib")
#endif

using namespace websocket_proxy;

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
    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("./log/WebsocketProxy.log");
    std::vector<spdlog::sink_ptr> sinks {stdout_sink, file_sink};
    auto logger = std::make_shared<spdlog::async_logger>("websocket_proxy_logger", sinks.begin(), sinks.end(), spdlog::thread_pool(), spdlog::async_overflow_policy::overrun_oldest);
    spdlog::set_default_logger(logger);
    spdlog::flush_on(spdlog::level::trace);
#else
    auto async_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>("./Log/WebsocketProxy.log", 524288000, 5);
    auto async_logger = std::make_shared<spdlog::async_logger>("async_logger", async_sink, spdlog::thread_pool(), spdlog::async_overflow_policy::overrun_oldest);
    spdlog::set_default_logger(async_logger);
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
    spdlog::flush_every(std::chrono::seconds(2));

    SPDLOG_INFO(std::format("Start WebsocketProxy {} ...", VERSION));
    WebsocketProxy proxy(server_queue_size);
    proxy.run();
    SPDLOG_INFO("WebsocketProxy Exit.");
    spdlog::shutdown();
}