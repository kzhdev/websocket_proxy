// zorro_websockets_proxy.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <signal.h>
#include <algorithm>
#include <cctype>
#include "zorro_websocket_proxy.h"

#pragma comment(lib, "Ws2_32.lib")

#define L_OFF 0
#define L_ERR		(1 << 0)
#define	L_WARN	(1 << 1)
#define	L_NOTICE	(1 << 2)
#define	L_INFO	(1 << 3)
#define	L_DEBUG	(1 << 4)
#define	L_PARSER	(1 << 5)
#define	L_HEADER	(1 << 6)
#define	L_EXT		(1 << 7)
#define	L_CLIENT	(1 << 8)
#define	L_LATENCY	(1 << 9)
#define	L_USER	(1 << 10)
#define	L_THREAD	(1 << 11)

using namespace zorro::websocket;

namespace {
    ZorroWebsocketProxy* the_proxy = nullptr;
}

void sigHandler(int sig) {
    if (the_proxy) {
        the_proxy->stop();
    }
    exit(EXIT_SUCCESS);
}

/**
* Usage:
* ZorroWebsocketsProxy.exe [-s <server_queue_size>] [-l <logging_level>]
* 
* Arguments:
*   -s [optional]: Specify server to client queue size in Byte. Default to 16777216 Bytes.
*   -l [optional]: Specify logging level. By default, logging will be disabled in release build.
*                  Valid Logging level: OFF, ERROR, WARNING, INFO, DEBUG* 
*/
int main(int argc, char* argv[])
{
    uint32_t server_queue_size = 1 << 24;   // 16MB
#ifdef _DEBUG
    int32_t level = L_ERR | L_WARN | L_NOTICE | L_USER;
#else
    int32_t level = L_ERR | L_WARN | L_USER;
#endif

    for (int i = 1; i < argc - 1; ++i) {
        if (_stricmp(argv[i], "-l") == 0) {
            std::string l = argv[++i];
            std::transform(l.begin(), l.end(), l.begin(), std::tolower);
            if (l == "off") {
                level = L_OFF;
            }
            else if (l == "error") {
                level = L_ERR;
            }
            else if (l == "warning") {
                level = L_ERR | L_WARN;
            }
            else if (l == "info") {
                level = L_ERR | L_WARN | L_NOTICE | L_USER;
            }
            else if (l == "debug") {
                level = L_ERR | L_WARN | L_NOTICE | L_USER | L_INFO;
            }
            else if (l == "trace") {
                level = L_ERR | L_WARN | L_NOTICE | L_USER | L_INFO | L_DEBUG;
            }
        }
        else if (_stricmp(argv[i], "-s") == 0) {
            server_queue_size = atoi(argv[++i]);
        }
    }

    signal(SIGINT, sigHandler);

    try {
        std::cout << "ZorroWebsocketProxy Started." << std::endl;
        std::unique_ptr<ZorroWebsocketProxy> proxy = std::make_unique<ZorroWebsocketProxy>(server_queue_size, level);
        the_proxy = proxy.get();
        proxy->run();
    }
    catch (...) {}
    std::cout << "ZorroWebsocketProxy Exit." << std::endl;
}