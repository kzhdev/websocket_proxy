# WebsocketProxy

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)

A high-performance C++ framework that enables multiple clients to share a single WebSocket connection through efficient inter-process communication (IPC) using shared memory queues.

## Overview

WebsocketProxy solves the problem of connection limitations by allowing multiple client applications to share a single WebSocket connection. Originally designed for [Alpaca Markets](https://alpaca.markets/), which restricts accounts to one WebSocket connection, it enables multiple trading strategies to receive real-time market data simultaneously.

**Key Features:**
- **Zero-Copy IPC**: Lock-free shared memory queues for high-performance communication
- **Protocol-Agnostic**: Transparent message pass-through works with any WebSocket API
- **Automatic Lifecycle Management**: Server spawns on first client connection and terminates when all clients disconnect
- **Header-Only Client**: Lightweight, easy-to-integrate client library
- **Multi-Platform**: Windows support with cross-platform design
- **Resource Efficient**: Single WebSocket connection serves unlimited clients

## Architecture

```
┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│  Client 1   │     │  Client 2   │     │  Client N   │
│             │     │             │     │             │
└──────┬──────┘     └──────┬──────┘     └──────┬──────┘
       │                   │                   │
       │    Shared Memory Queues (IPC)         │
       └───────────────┬───────────────────────┘
                       │
              ┌────────▼────────┐
              │ Proxy Server    │
              │ (websocket_     │
              │  proxy.exe)     │
              └────────┬────────┘
                       │
                       │ Single WebSocket
                       │
              ┌────────▼────────┐
              │ Remote Server   │
              │ (e.g., Alpaca)  │
              └─────────────────┘
```

### Components

1. **Proxy Server** (`websocket_proxy.exe`)
   - Maintains single WebSocket connection to remote server
   - Manages IPC via lock-free shared memory queues
   - Automatically spawned by first client connection
   - Routes messages between clients and remote server
   - Terminates when all clients disconnect

2. **Proxy Client** (Header-only library)
   - Lightweight client library for your applications
   - Derives from `WebsocketProxyCallback` interface
   - Handles spawning and communication with proxy server
   - Provides async/sync WebSocket operations
   - Automatic heartbeat and reconnection handling

## Quick Start

### Installation

Download the latest release from the [releases page](https://github.com/kzhdev/websocket_proxy/releases), extract it, and copy the contents to your project:

```bash
# Extract release
unzip websocket_proxy_v1.1.1.1.zip

# Copy headers to your project
cp -r include /path/to/your/project/

# Copy proxy server executable
cp bin/websocket_proxy.exe /path/to/your/project/bin/
```

### Usage Example

1. **Implement the callback interface:**

```cpp
#include <websocket_proxy/websocket_proxy_client.h>

class MyWebSocketClient : public websocket_proxy::WebsocketProxyCallback {
public:
    void onWebsocketProxyServerDisconnected() override {
        // Handle proxy server disconnect
    }

    void onWebsocketOpened(uint64_t id) override {
        // WebSocket connection established
        std::cout << "Connected: " << id << std::endl;
    }

    void onWebsocketClosed(uint64_t id) override {
        // WebSocket connection closed
    }

    void onWebsocketError(uint64_t id, const char* err, uint32_t len) override {
        // Handle WebSocket errors
        std::cerr << "Error: " << std::string(err, len) << std::endl;
    }

    void onWebsocketData(uint64_t id, const char* data, uint32_t len, uint32_t remaining) override {
        // Process incoming data
        std::string message(data, len);
        std::cout << "Received: " << message << std::endl;
    }
};
```

2. **Create and use the client:**

```cpp
#include <websocket_proxy/websocket_proxy_client.h>

int main() {
    MyWebSocketClient callback;

    // Initialize client with callback and proxy server path
    websocket_proxy::WebsocketProxyClient client(
        &callback,
        "MyClient",                                    // Client name
        "./bin/websocket_proxy.exe"                    // Proxy server path
    );

    // Open WebSocket connection
    auto [id, is_new] = client.openWebSocket(
        "wss://stream.data.alpaca.markets/v2/iex",    // WebSocket URL
        "your_api_key"                                 // API key (optional)
    );

    if (id) {
        // Send messages
        std::string msg = R"({"action":"subscribe","bars":["AAPL"]})";
        client.send(id, msg.c_str(), msg.size());

        // Keep running...
        std::this_thread::sleep_for(std::chrono::seconds(60));

        // Close connection
        client.closeWebSocket(id);
    }

    return 0;
}
```

For a complete example, see the [Alpaca WebSocket client](https://github.com/kzhdev/websocket_proxy/tree/main/example) in the `example/` directory.


## Building From Source

### Requirements

- **C++20** compatible compiler (MSVC 2019+, GCC 10+, Clang 12+)
- **CMake** 3.16 or higher
- **vcpkg** for dependency management
- **Dependencies:**
  - Boost.Beast (WebSocket client)
  - OpenSSL (TLS/SSL support)
  - [slick_logger](https://github.com/SlickQuant/slick_logger) (automatically fetched)
  - [slick_queue](https://github.com/SlickQuant/slick_queue) (automatically fetched)

### Setup vcpkg

```bash
# Clone and bootstrap vcpkg
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg

# Windows
./bootstrap-vcpkg.bat

# Linux/macOS
./bootstrap-vcpkg.sh

# Install dependencies
./vcpkg install boost-beast:x64-windows-static
./vcpkg install openssl:x64-windows-static
```

### Build Steps

```bash
# Clone repository
git clone https://github.com/kzhdev/websocket_proxy.git
cd websocket_proxy

# Configure with CMake (Windows example)
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build ./build --config Release -j

# Output will be in build/bin/Release/websocket_proxy.exe
# Headers in build/dist/include/
```

### Build Options

```bash
# Build without example
cmake -S . -B build -DBUILD_EXAMPLE=OFF

# Debug build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug

# Create distribution package (Release only)
# Automatically creates websocket_proxy_<version>.zip in build/dist/
cmake --build ./build --config Release
```

### Install

```bash
# Install to system (optional)
cmake --install ./build --prefix /usr/local
```

## API Reference

### WebsocketProxyCallback Interface

Implement this interface to handle WebSocket events:

```cpp
class WebsocketProxyCallback {
public:
    virtual ~WebsocketProxyCallback() = default;

    // Called when proxy server disconnects
    virtual void onWebsocketProxyServerDisconnected() = 0;

    // Called when WebSocket connection opens
    virtual void onWebsocketOpened(uint64_t id) = 0;

    // Called when WebSocket connection closes
    virtual void onWebsocketClosed(uint64_t id) = 0;

    // Called on WebSocket errors
    virtual void onWebsocketError(uint64_t id, const char* err, uint32_t len) = 0;

    // Called when data is received
    // remaining: bytes remaining in current message (for fragmented messages)
    virtual void onWebsocketData(uint64_t id, const char* data, uint32_t len, uint32_t remaining) = 0;

    // Optional: Logging callbacks
    virtual void logError(std::function<std::string()>&&) {}
    virtual void logWarning(std::function<std::string()>&&) {}
    virtual void logInfo(std::function<std::string()>&&) {}
    virtual void logDebug(std::function<std::string()>&&) {}
};
```

### WebsocketProxyClient Methods

```cpp
// Constructor
WebsocketProxyClient(
    WebsocketProxyCallback* callback,
    std::string&& name,              // Client identifier
    std::string&& proxy_exe_path     // Path to websocket_proxy.exe
);

// Open WebSocket (synchronous) - returns (connection_id, is_new_connection)
std::pair<uint64_t, bool> openWebSocket(
    const std::string& url,
    const std::string& api_key = ""
);

// Open WebSocket (asynchronous)
bool openWebSocketAsync(
    const std::string& url,
    const std::string& api_key = ""
);

// Close WebSocket (id=0 closes all)
bool closeWebSocket(uint64_t id = 0);

// Send data to WebSocket
void send(uint64_t id, const char* msg, uint32_t len);

// Subscribe to symbol (for trading APIs)
bool subscribe(
    uint64_t id,
    const std::string& symbol,
    const char* subscription_request,
    uint32_t request_len,
    SubscriptionType type,
    bool& existing
);

// Unsubscribe from symbol
bool unsubscribe(
    uint64_t id,
    const std::string& symbol,
    const char* unsubscription_request,
    uint32_t request_len
);

// Set logging level
bool setLogLevel(LogLevel::level_enum level);

// Get server process ID
uint64_t serverId() const noexcept;
```

## Performance

- **Zero-copy IPC**: Shared memory queues eliminate data copying between processes
- **Lock-free**: Wait-free algorithms for high-throughput communication
- **Low latency**: Optimized message routing with minimal overhead
- **Scalable**: Supports unlimited clients with constant memory per client

## Use Cases

- **Trading Systems**: Share market data WebSocket across multiple strategies
- **Real-time Data**: Multiple consumers for single data stream
- **API Rate Limiting**: Multiplex API connections with connection limits
- **Resource Optimization**: Reduce connection overhead in microservices
- **Development/Testing**: Test multiple clients with single connection

## Troubleshooting

### Server fails to start

- Ensure `websocket_proxy.exe` path is correct
- Check Windows Defender/Firewall isn't blocking execution
- Verify shared memory permissions

### Connection timeout

- Increase timeout in `waitForResponse()` calls
- Check network connectivity to remote server
- Verify WebSocket URL is correct

### Memory issues

- Monitor shared queue sizes for memory leaks
- Ensure proper cleanup on client disconnect
- Check for deadlocks in message handling

## Contributing

Contributions are welcome! Please:

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.

## Acknowledgments

- Built with [Boost.Beast](https://www.boost.org/doc/libs/1_84_0/libs/beast/doc/html/index.html) for WebSocket implementation
- Uses [slick_queue](https://github.com/SlickQuant/slick_queue) for lock-free IPC
- Logging powered by [slick_logger](https://github.com/SlickQuant/slick_logger)

## Support

- **Issues**: [GitHub Issues](https://github.com/kzhdev/websocket_proxy/issues)
- **Discussions**: [GitHub Discussions](https://github.com/kzhdev/websocket_proxy/discussions)

---

**Version**: 1.1.1.0
**Author**: Kun Zhao
**Copyright**: © 2024-2025