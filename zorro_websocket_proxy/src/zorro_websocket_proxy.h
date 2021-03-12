#pragma once

#define WIN32_LEAN_AND_MEAN

#include <atomic>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include "types.h"
#include "utils.h"

namespace zorro {
namespace websocket {

    class Websocket;

    class ZorroWebsocketProxy final {
        std::atomic_bool run_{ true };
        SHM_QUEUE_T client_queue_;
        SHM_QUEUE_T server_queue_;
        uint64_t client_index_ = 0;
        uint64_t last_heartbeat_time_ = 0;
        uint64_t shutdown_time_ = 0;
        std::thread ws_thread_;
        const DWORD pid_;
        HANDLE hMapFile_ = nullptr;
        LPVOID lpvMem_ = nullptr;
        bool own_shm_ = false;
        bool clientSeen_ = false;
        std::atomic<DWORD>* owner_pid_ = nullptr;
        std::string exec_path_;

        struct ClientInfo {
            DWORD pid;
            uint64_t last_heartbeat_time;
        };
        std::unordered_map<DWORD, ClientInfo> clients_;
        std::unordered_map<uint32_t, std::shared_ptr<Websocket>> websocketsById_;
        std::unordered_map<std::string, std::shared_ptr<Websocket>> websocketsByUrl_;
        slick::SlickQueue<uint32_t> closed_sockets_;
        uint64_t closed_sockets_index_ = 0;

    public:
        ZorroWebsocketProxy(uint32_t ws_queue_size);
        ~ZorroWebsocketProxy();

        void run(const char* executable_path, int32_t logLevel);
        void stop() noexcept { run_.store(false, std::memory_order_release); }

    private:
        friend class Websocket;

        void handleClientMessage(Message& msg);
        void handleClientRegistration(Message& msg);
        void unregisterClient(uint32_t pid);
        void handleClientHeartbeat(Message& msg);
        void openWs(Message& msg);
        void closeWs(Message& msg);
        void closeWs(uint32_t id, DWORD pid);
        void sendWsRequest(Message& msg);
        ClientInfo* getClient(DWORD pid);
        bool checkHeartbeats();
        void sendMessage(Message* msg, uint64_t idex, uint32_t size);
        void onWsOpened(uint32_t id, DWORD initicator);
        void onWsClosed(uint32_t id);
        void onWsError(uint32_t id, const char* err, size_t len);
        void onWsData(uint32_t id, const char* err, size_t len, size_t remaining);
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
            auto size = sizeof(Message);
            auto index = server_queue_.reserve(size);
            auto msg = reinterpret_cast<Message*>(server_queue_[index]);
            memset(msg, 0, size);
            msg->pid = pid_;
            return std::make_tuple(msg, index, size);
        }
    };

}
}