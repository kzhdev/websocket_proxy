#pragma once

#include "slick_queue.h"
#include <atomic>
#include <chrono>

namespace zorro {
namespace websocket {

#define CLIENT_TO_SERVER_QUEUE "Local\\ZorroWebsocketProxy_client_server"
#define SERVER_TO_CLIENT_QUEUE "Local\\ZorroWebsocketProxy_server_client"
#define HEARTBEAT_INTERVAL 500  // 500ms
#define HEARTBEAT_TIMEOUT 15000 // 15s

#pragma pack(1)
    struct Message {
        enum Type : uint8_t {
            Regster,
            Unregister,
            OpenWs,
            CloseWs,
            Heartbeat,
            WsRequest,
            WsData,
            WsError,
            Subscribe,
            Unsubscribe,
        };

        enum Status : uint8_t {
            PENDING,
            SUCCESS,
            FAILED,
        };

        DWORD pid;
        Type type;
        std::atomic<Status> status;
        uint8_t data[0];
    };

    struct RegisterMessage {
        char name[32];
        // response
        DWORD server_pid;
        char err[256];
    };

    struct WsOpen {
        char url[512];
        // response
        uint32_t id;
        DWORD initiator;
        bool new_connection;
        char err[256];
    };

    struct WsClose {
        uint32_t id;
    };

    struct WsSubscription {
        char symbol[256];
        uint32_t id;
        size_t request_len;
        bool existing;
        char request[0];
    };

    struct WsRequest {
        uint32_t id;
        size_t len;
        char data[0];
    };

    struct WsError {
        uint32_t id;
        size_t len;
        char err[0];
    };

    struct WsData {
        uint32_t id;
        size_t len;
        size_t remaining;
        char data[0];
    };
#pragma pack()

    typedef slick::SlickQueue<uint8_t> SHM_QUEUE_T;

}
}