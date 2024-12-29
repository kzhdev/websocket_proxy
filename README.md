# AlpacaWebsocketProxy: Share a Single WebSocket Connection for Multiple Clients (C++)
AlpacaWebSocketProxy provides a C++ WebSocket proxy framework that empowers you to share a single WebSocket connection among multiple clients, maximizing resource utilization and streamlining real-time data access.

## Built for Efficiency: Serving Multiple Clients with One Connection
Designed initially for the [Alpaca.Market](https://alpaca.markets/), which offers data via both REST and WebSockets. However, Alpaca limits the number of concurrent WebSocket connections per account. AlpacaWebSocketProxy solves this by enabling multiple strategies under the same account to receive real-tine market data updates through a single, shared WebSocket connection.

## Components and Communication
AlpacaWebSocketProxy consists of two components: 
1. Standalone proxy server executable: This executable acts as the central communication hub. It manages the connection to the WebSocket server and handles data forwarding between clients. It's launched upon the first client attempting a WebSocket connection, ensuring only one server instance runs simultaneously. Any subsequent server launch attempts automatically terminate, preventing resource conflicts. The server continues execution until all connected clients are closed.
2. Header-only Proxy Client: This lightweight client library integrates into your individual applications. It manages communication with the proxy server and provides a familiar interface for sending and receiving data.

## Benefits of AlpacaWebSocketProxy
* Efficient Resource Utilization: Share a single WebSocket connection, reducing overhead and improving resource management.
* Real-Time Data Streamlining: Multiple trading strategies within an account gain access to crucial market updates concurrently.
* Simplified Integration: The header-only client library enables effortless integration with your existing client applications.
* Shared Memory Optimization: Communication between server and clients leverages shared memory ring buffers, ensuring high-performance data exchange.