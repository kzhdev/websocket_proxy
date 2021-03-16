#pragma once

#include <slicksocket/websocket_client.h>
#include <slicksocket/callback.h>
#include <lws/libwebsockets.h>
#include "slick_queue.h"
#include <unordered_set>

namespace zorro {
namespace websocket {

    class Websocket : public slick::net::websocket_client, private slick::net::client_callback_t {
        friend class ZorroWebsocketProxy;
        ZorroWebsocketProxy* proxy_ = nullptr;
        uint32_t id_ = 0;
        DWORD initiator_ = 0;
        std::unordered_set<DWORD> clients_;

        enum Status : uint8_t {
            CONNECTING,
            CONNECTED,
            DISCONNECTING,
            DISCONNECTED,
        };
        std::atomic<Status> status_{ Status::DISCONNECTED };


    public:
        Websocket(ZorroWebsocketProxy* proxy, uint32_t id, std::string url) 
            : websocket_client(this, std::move(url), "", proxy->exec_path_ + "/cert.pem", -1, true)
            , proxy_(proxy)
            , id_(id)
        {
            clients_.reserve(128);
        }

        virtual ~Websocket() = default;

        uint32_t id() const noexcept {
            return id_;
        }

        std::unordered_set<DWORD>& clients() noexcept {
            return clients_;
        }

        bool open(DWORD initiator) {
            initiator_ = initiator;
            status_.store(Status::CONNECTING, std::memory_order_relaxed);

            if (!connect()) {
                status_.store(Status::DISCONNECTED, std::memory_order_relaxed);
                return false;
            }

            auto retry = 150;
            while (retry--) {
                auto status = status_.load(std::memory_order_relaxed);
                if (status != Status::CONNECTING) {
                    break;
                }
                if (!proxy_->sendHeartbeat()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
            return status_ == Status::CONNECTED;
        }

    private:
        void on_connected() override {
            lwsl_user("Websocket connected. id=%d\n", id_);
            proxy_->onWsOpened(id_, initiator_);
            status_.store(Status::CONNECTED, std::memory_order_release);
        }

        void on_disconnected() override {
            lwsl_user("Websocket disconnected. id=%d\n", id_);
            status_.store(Status::DISCONNECTED, std::memory_order_release);
            proxy_->onWsClosed(id_); 
        }

        void on_error(const char* msg, size_t len) override {
            proxy_->onWsError(id_, msg, len);
            if (msg && len) {
                lwsl_user("OnError %.*s, id_=%d\n", len, msg, id_);
            }
            else {
                lwsl_user("Unkown error occurred, id=%d\n", id_);
            }
        }

        void on_data(const char* data, size_t len, size_t remaining) override {
            proxy_->onWsData(id_, data, len, remaining);
            //lwsl_user("%.*s(%ld, %ld)\n", len, data, len, remaining);
        }
    };
}
}