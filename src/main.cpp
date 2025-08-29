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

#include "websocket_proxy.h"
#include <websocket_proxy/version.h>
#include <csignal>

#ifdef _WIN32
#pragma comment(lib, "Ws2_32.lib")
#endif

using namespace websocket_proxy;
using namespace slick_logger;

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
    Logger::instance().clear_sinks();
    LogConfig config;
    config.queue_size = 65536;

#ifdef DEBUG
    config.sinks.push_back(std::make_shared<ConsoleSink>(true, true));
    config.min_level = slick_logger::LogLevel::L_TRACE;
#endif
    // Configure rotation
    RotationConfig rotation;
    rotation.max_file_size = 200 * 1024 * 1024;  // 200MB
    rotation.max_files = 10;                     // keep last 10 files
    config.sinks.push_back(std::make_shared<RotatingFileSink>("./Log/WebsocketProxy.log", rotation));

    uint32_t server_queue_size = 1 << 24;   // 16MB
    [[maybe_unused]] bool log_level_set = false;
    for (int i = 1; i < argc - 1; ++i) {
        if (_stricmp(argv[i], "-l") == 0) {
            std::string l = argv[++i];
            std::transform(l.begin(), l.end(), l.begin(), [](char c){ return std::tolower(c); });
            log_level_set = true;
            if (l == "off") {
                config.min_level = slick_logger::LogLevel::L_OFF;
            }
            else if (l == "critical") {
                config.min_level = slick_logger::LogLevel::L_FATAL;
            }
            else if (l == "error") {
                config.min_level = slick_logger::LogLevel::L_ERROR;
            }
            else if (l == "warning") {
                config.min_level = slick_logger::LogLevel::L_WARN;
            }
            else if (l == "info") {
                config.min_level = slick_logger::LogLevel::L_INFO;
            }
            else if (l == "debug") {
                config.min_level = slick_logger::LogLevel::L_DEBUG;
            }
            else if (l == "trace") {
                config.min_level = slick_logger::LogLevel::L_TRACE;
            }
            else {
                log_level_set = false;
            }
        }
        else if (_stricmp(argv[i], "-s") == 0) {
            server_queue_size = atoi(argv[++i]);
        }
    }

    Logger::instance().init(config);

    LOG_INFO(std::format("Start WebsocketProxy {} ...", VERSION));
    WebsocketProxy proxy(server_queue_size);
    proxy.run();
    LOG_INFO("WebsocketProxy Exit.");
}