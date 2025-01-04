#include "alpaca_websocket_client.h"
#include <iostream>
#include <csignal>

using namespace websocket_proxy;
using namespace alpaca_websocket;

void usage(const char* app)
{
    std::cout << "Usage: " << app << " <Alpaca_API_KEY> <Alpaca_API_SECRET> -u [url]" << std::endl;
    std::cout << "Options: " << std::endl;
    std::cout << "    -u [URL]: WebSocket url. default to wss://stream.data.alpaca.markets/v2/iex" << std::endl;
    std::cout << "    -p [websocket_proxy.exe path]: websocket_proxy.exe path. default to ./build/bin/<build config>/websocket_proxy.exe" << std::endl;
}

void signalHandler(int signum) {
    exit(signum);
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        usage(argv[0]);
        return 1;
    }

    std::string url = "wss://stream.data.alpaca.markets/v2/iex";
    std::string api_key;
    std::string api_secret;

    for (auto i = 1; i < argc; )
    {
        if (strcmp(argv[i], "-u") == 0)
        {
            url = argv[++i];
        }
        else if (api_key.empty())
        {
            api_key = argv[i++];
        }
        else
        {
            api_secret = argv[i++];
        }
    }

    if (api_key.empty() || api_secret.empty())
    {
        usage(argv[0]);
        return 1;
    }

#ifdef DEBUG
    std::string proxy_exe("./build/bin/Debug/websocket_proxy.exe");
#else
    std::string proxy_exe("./build/bin/Release/websocket_proxy.exe");
#endif

    signal(SIGINT, signalHandler);
    
    AlpacaWebSocketClient client(std::move(proxy_exe));
    if (client.open(std::move(url), api_key.c_str(), api_secret.c_str()))
    {
        client.subscribe("AAPL");

        while(true);    // Ctr + C to exit
    }
}