# WebsocketProxy: Share a Single WebSocket Connection for Multiple Clients (C++)
WebsocketProxy is a C++ framework that allows multiple clients to share a single WebSocket connection, optimizing resource usage and enhancing real-time data access.

## Efficiently Serve Multiple Clients
Originally designed for [Alpaca.Market](https://alpaca.markets/), which provides data through both REST and WebSockets. However, Alpaca only allows one WebSocket connection per account. WebsocketProxy enables multiple strategies under the same account to receive real-time market data updates via a single shared WebSocket connection. WebsocketProxy doesn't have any Alpaca API protocol. It passes through messages between clients and server, making it suitable for other WebSocket connections as well.

## Components
WebsocketProxy comprises two main components:
1. **Standalone Proxy Server**: This executable serves as the central communication hub, managing the WebSocket server connection and data forwarding between clients. It launches with the first client connection attempt, ensuring only one server instance runs at a time. Subsequent server launch attempts are automatically terminated to prevent conflicts. The server continues running until all clients disconnect.
2. **Header-only Proxy Client**: This lightweight client library integrates seamlessly into your applications, managing communication with the proxy server and providing an interface for data transmission.

## Installation

To install WebsocketProxy, simply download the latest release from the [releases page](https://github.com/kzhdev/websocket_proxy/releases), unzip the package, and copy the `include` folder to your project directory.

Then, derive from the `WebsocketProxyCallback` class and implement the corresponding callback functions. For reference, check the Alpaca WebSocket client example in the [example](https://github.com/kzhdev/websocket_proxy/tree/main/example) directory.


## Build From the Source Code

To build WebsocketProxy, follow these steps:

### Installing Dependencies

WebsocketProxy relies on several external libraries. You can install these dependencies using [vcpkg](https://github.com/microsoft/vcpkg). Follow these steps to install the required libraries:

1. **Install vcpkg**:
    ```sh
    git clone https://github.com/microsoft/vcpkg.git
    cd vcpkg
    ./bootstrap-vcpkg.sh
    ```

2. **Install Required Libraries**:
    ```sh
    ./vcpkg install spdlog:x64-windows-static
    ./vcpkg install boost-beast:x64-windows-static
    ./vcpkg install openssl:x64-windows-static
    ```

3. **Integrate vcpkg with CMake**: 
    Add the following line to your CMake configuration to use vcpkg:
    ```sh
    cmake .. -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
    ```

### Build WebsocketProxy

1. **Clone the Repository**:
    ```sh
    git clone https://github.com/kzhdev/websocket_proxy.git
    cd websocket_proxy
    ```

2. **Build the Proxy Server**: 
    Ensure you have a C++ compiler and CMake installed. Then, run the following commands:
    ```sh
    cmake -S . -B build
    cmake --build ./build --config Release -j 18
    ```

3. **Include the Client Library**: 
    Copy the include folder from the repository to your project:
    ```sh
    cp -r include /path/to/your/project/include/
    ```
4. **Derive from the `WebsocketProxyCallback` class and implement the corresponding callback functions**
    
    For reference, check the Alpaca WebSocket client example in the [example](https://github.com/kzhdev/websocket_proxy/tree/main/example) directory.

## Contributing

Contributions are welcome! Please open an issue or submit a pull request on [GitHub](https://github.com/kzhdev/websocket_proxy).

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.