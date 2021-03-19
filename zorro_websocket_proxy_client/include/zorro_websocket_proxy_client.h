#pragma once

#include <Windows.h>
#include <cstdint>
#include <thread>
#include <atomic>
#include <unordered_set>
#include <functional>
#include "types.h"
#include "utils.h"

namespace zorro {
namespace websocket {

    enum LogLevel : uint8_t {
        L_OFF,
        L_ERROR,
        L_WARNING,
        L_INFO,
        L_DEBUG,
        L_TRACE,
        L_TRACE2,
    };

    typedef int (*broker_error) (const char*);
    typedef int (*broker_progress) (const int percent);
    typedef std::function<void(LogLevel level, const std::string&)> log;

    inline void NoLog(LogLevel level, const std::string& msg) {}

    class WebsocketProxyCallback {
    public:
        virtual ~WebsocketProxyCallback() = default;
        virtual void onWebsocketProxyServerDisconnected() = 0;
        virtual void onWebsocketOpened(uint32_t id) = 0;
        virtual void onWebsocketClosed(uint32_t id) = 0;
        virtual void onWebsocketError(uint32_t id, const char* err, size_t len) = 0;
        virtual void onWebsocketData(uint32_t id, const char* data, size_t len, size_t remaining) = 0;
    };

    class ZorroWebsocketProxyClient {
    public:
        ZorroWebsocketProxyClient(WebsocketProxyCallback* callback, std::string&& name, broker_error broker_err, broker_progress broker_prog);
        virtual ~ZorroWebsocketProxyClient();

        void setLogger(log log_func = NoLog) noexcept {
            log_ = log_func;
        }

        DWORD serverId() const noexcept { return server_pid_; }

        std::pair<uint32_t, bool> openWebSocket(const std::string& url);
        bool closeWebSocket(uint32_t id = 0);
        void send(uint32_t id, const char* msg, size_t len);

    private:
        void brokerError(const std::string& msg) const noexcept {
            broker_error_(msg.c_str());
        }

        bool connect();
        bool spawnWebsocketsProxyServer();
        bool _register();
        void unregister(bool destroying = false);
        void sendMessage(Message* msg, uint64_t index, uint32_t size);
        bool waitForResponse(Message* msg, uint32_t timeout = 10000);
        bool sendHeartbeat(uint64_t now);
        void doWork();
        void handleWsOpen(Message* msg);
        void handleWsClose(Message* msg);
        void handleWsError(Message* msg);
        void handleWsData(Message* msg);

        template<typename T>
        std::tuple<Message*, uint64_t, uint32_t> reserveMessage(uint32_t data_size = 0) {
            auto size = get_message_size<T>(data_size);
            auto index = client_queue_->reserve(size);
            auto msg = reinterpret_cast<Message*>((*(client_queue_.get()))[index]);
            memset(msg, 0, size);
            msg->pid = pid_;
            return std::make_tuple(msg, index, size);
        }

        std::tuple<Message*, uint64_t, uint32_t> reserveMessage() {
            auto size = sizeof(Message);
            auto index = client_queue_->reserve(size);
            auto msg = reinterpret_cast<Message*>((*(client_queue_.get()))[index]);
            memset(msg, 0, size);
            msg->pid = pid_;
            return std::make_tuple(msg, index, size);
        }
        
    private:
        WebsocketProxyCallback* callback_ = nullptr;
        broker_error broker_error_ = nullptr;
        broker_progress broker_progress_ = nullptr;
        std::unique_ptr<SHM_QUEUE_T> client_queue_;
        std::unique_ptr<SHM_QUEUE_T> server_queue_;
        uint64_t server_queue_index_ = 0;
        uint64_t last_heartbeat_time_ = 0;
        uint64_t last_server_heartbeat_time_ = 0;
        uint32_t id_ = 0;
        const DWORD pid_;
        std::atomic<DWORD> server_pid_ = 0;
        std::atomic_bool run_{ false };
        std::string name_;
        std::unordered_set<uint32_t> websockets_;
        std::unique_ptr<std::thread> worker_thread_;
        log log_ = NoLog;
    };

}
}