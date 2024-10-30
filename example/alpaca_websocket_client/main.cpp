#include "alpaca_websocket_client.h"

using namespace websocket_proxy;
using namespace alpaca_websocket;

int main(int argc, char* argv[])
{
#ifdef DEBUG
    std::string proxy_exe("../Debug/websocket_proxy.exe");
#else
    std::string proxy_exe("../Release/websocket_proxy.exe");
#endif
    
    AlpacaWebSocketClient client(proxy_exe);
    client.openWebsocket();
}