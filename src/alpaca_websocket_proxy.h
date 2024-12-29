#pragma once

#include <atomic>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <alpaca_websocket_proxy/types.h>
#include <boost/asio/ssl.hpp>

#ifdef SPDLOG_ACTIVE_LEVEL
#undef SPDLOG_ACTIVE_LEVEL
#endif
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

namespace asio = boost::asio;            // from <boost/asio.hpp>
namespace ssl = asio::ssl;       // from <boost/asio/ssl.hpp>

namespace alpaca_websocket_proxy {

class Websocket;

class AlpacaWebsocketProxy final {
    std::atomic_bool run_{ true };
    SHM_QUEUE_T client_queue_;
    SHM_QUEUE_T server_queue_;
    uint64_t client_index_ = 0;
    uint64_t last_heartbeat_time_ = 0;
    uint64_t shutdown_time_ = 0;
    // std::thread ws_thread_;
    const uint64_t pid_;
    HANDLE hMapFile_ = nullptr;
    LPVOID lpvMem_ = nullptr;
    bool own_shm_ = false;
    std::atomic<uint64_t>* owner_pid_ = nullptr;
    std::string exec_path_;

    struct ClientInfo {
        uint64_t pid;
        uint64_t last_heartbeat_time;
    };
    std::unordered_map<uint64_t, ClientInfo> clients_;
    std::unordered_map<uint64_t, std::shared_ptr<Websocket>> websocketsById_;

    struct WebsocketKey
    {
        std::string url;
        std::string api_key;
    };

    struct WebsocketKeyHash
    {
        std::size_t operator()(const WebsocketKey& k) const
        {
            return std::hash<std::string>()(k.url) ^
                (std::hash<std::string>()(k.api_key) << 1);
        }
    };
 
    struct WebsocketKeyEqual
    {
        bool operator()(const WebsocketKey& lhs, const WebsocketKey& rhs) const
        {
            return lhs.url == rhs.url && lhs.api_key == rhs.api_key;
        }
    };
    std::unordered_map<WebsocketKey, std::shared_ptr<Websocket>, WebsocketKeyHash, WebsocketKeyEqual> websocketsByUrlApiKey_;
    slick::SlickQueue<uint64_t> closed_sockets_;
    uint64_t closed_sockets_index_ = 0;

    asio::io_context ioc_;
    ssl::context ctx_{ssl::context::tlsv12_client};


public:
    AlpacaWebsocketProxy(uint32_t server_queue_size);
    ~AlpacaWebsocketProxy();

    void run();
    void stop() noexcept { run_.store(false, std::memory_order_release); ioc_.stop(); }

private:
    friend class Websocket;

    void startHeartbeat();
    void processClientMessage();
    void handleClientMessage(Message& msg);
    void handleClientRegistration(Message& msg);
    void unregisterClient(uint64_t pid);
    void handleClientHeartbeat(Message& msg);
    void openWs(Message& msg);
    void openNewWs(Message& msg, WsOpen* req);
    void closeWs(Message& msg);
    void closeWs(uint64_t id, uint64_t pid);
    void sendWsRequest(Message& msg);
    void handleSubscribe(Message& msg);
    void handleUnsubscribe(Message& msg);
    ClientInfo* getClient(uint64_t pid);
    bool checkHeartbeats();
    bool sendHeartbeat();
    bool sendHeartbeat(uint64_t now);
    void sendMessageToClient(uint64_t idex, uint32_t size);
    void sendMessageToClient(uint64_t idex, uint32_t size, uint64_t now);
    void onWsOpened(uint64_t id, uint64_t client_pid);
    void onWsClosed(uint64_t id);
    void onWsError(uint64_t id, const char* err, uint32_t len);
    void onWsData(uint64_t id, const char* err, uint32_t len, uint32_t remaining);
    void removeClosedSockets();

    template<typename T>
    std::tuple<Message*, uint64_t, uint32_t> reserveMessage(uint32_t data_size = 0) {
        auto size = get_message_size<T>(data_size);
        auto index = server_queue_.reserve(size);
        auto msg = reinterpret_cast<Message*>(server_queue_[index]);
        memset(msg, 0, size);
        msg->pid = pid_;
        return std::make_tuple(msg, index, size);
    }

    std::tuple<Message*, uint64_t, uint32_t> reserveMessage() {
        uint32_t size = sizeof(Message);
        auto index = server_queue_.reserve(size);
        auto msg = reinterpret_cast<Message*>(server_queue_[index]);
        memset(msg, 0, size);
        msg->pid = pid_;
        return std::make_tuple(msg, index, size);
    }
};

}