#pragma once

#include "slick_queue.h"
#include <atomic>
#include <chrono>

namespace alpaca_websocket_proxy {

#define CLIENT_TO_SERVER_QUEUE "AlpacaWebsocketProxy_client_server"
#define SERVER_TO_CLIENT_QUEUE "AlpacaWebsocketProxy_server_client"
#define HEARTBEAT_INTERVAL 500  // 500ms
#define HEARTBEAT_TIMEOUT 15000 // 15s

#pragma warning( push )
#pragma warning( disable : 4200 )

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

    uint64_t pid;
    Type type;
    std::atomic<Status> status;
    uint8_t data[0];
};

struct RegisterMessage {
    char name[32];
    // response
    uint64_t server_pid;
    char err[256];
};

struct WsOpen {
    char url[512];
    char api_key[512];
    // response
    uint64_t client_pid;
    uint64_t id;
    bool new_connection;
    char err[256];
};

struct WsClose {
    uint64_t id;
};

enum SubscriptionType : uint8_t
{
    None = 0,
    Quotes = 1,
    Trades = 1 << 1,
};

struct WsSubscription {
    char symbol[256];
    uint64_t id;
    uint32_t request_len;
    bool existing;
    SubscriptionType type;
    char request[0];
};

struct WsRequest {
    uint64_t id;
    uint32_t len;
    char data[0];
};

struct WsError {
    uint64_t id;
    uint32_t len;
    char err[0];
};

struct WsData {
    uint64_t id;
    uint32_t len;
    uint32_t remaining;
    char data[0];
};
#pragma pack()
#pragma warning( pop )

typedef slick::SlickQueue<uint8_t> SHM_QUEUE_T;

}