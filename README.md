# ZorroWebsocketProxy
ZorroWebsocketProxy is a C++ WebSocket prroxy framework initally created for Zorro-Trader. It can be used for any application to share one WebSockets connection for multiple application instances.

## Overview
ZorroWebsocketProxy includes two components: a standalone proxy server excutable and a proxy client static library. The proxy server and client communicate with each other through shared memory ring buffers.

The proxy server is spawned when the first client trying to open a Websocket. The server makes sure that there is only one server running. If for any reason a second server is launched, it will shut itself down. The proxy server will keeps runing until the last client instances are closed.

