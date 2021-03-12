#include "pch.h"
#include "framework.h"
#include <string>
#include <filesystem>
#include <thread>
#include <chrono>
#include "zorro_websocket_proxy_client.h"

using namespace zorro::websocket;

namespace {
    constexpr TCHAR szName[] = TEXT("ZorroWebsocketProxy");
    constexpr uint32_t BF_SZ = 64;
}


ZorroWebsocketProxyClient::ZorroWebsocketProxyClient(WebsocketProxyCallback* callback, std::string&& name, broker_error broker_err, broker_progress broker_prog)
    : callback_(callback)
    , broker_error_(broker_err)
    , broker_progress_(broker_prog)
    , pid_(GetCurrentProcessId())
    , name_(std::move(name))
    , worker_thread_([this]() { doWork(); })
{}

ZorroWebsocketProxyClient::~ZorroWebsocketProxyClient() {
    if (server_pid_.load(std::memory_order_relaxed)) {
        unregister();
    }
    run_.store(false, std::memory_order_release);
    if (worker_thread_.joinable()) {
        // join causes dead lock
        //worker_thread_.join();
        worker_thread_.detach();
    }
}

bool ZorroWebsocketProxyClient::connect() {
    if (!spawnWebsocketsProxyServer()) {
        log_(L_ERROR, "Failed to spawn websocket proxy");
        return false;
    }

    return _register();
}

void ZorroWebsocketProxyClient::sendMessage(Message* msg, uint64_t index, uint32_t size) {
    msg->status.store(Message::Status::PENDING, std::memory_order_relaxed);
    client_queue_->publish(index, size);
    last_heartbeat_time_ = get_timestamp();
}

bool ZorroWebsocketProxyClient::waitForResponse(Message* msg, uint32_t timeout) const {
    auto start = get_timestamp();
    while (msg->status.load(std::memory_order_relaxed()) == Message::Status::PENDING) {
        if ((get_timestamp() - start) > timeout) {
            return false;
        }
        broker_progress_(1);
        std::this_thread::yield();
    }
    return true;
}

bool ZorroWebsocketProxyClient::spawnWebsocketsProxyServer() {
    if (!isProcessRunning(L"zorro_websocket_proxy.exe")) {
        auto path = std::filesystem::current_path();
        path.append("Plugin\\websocket_proxy\\zorro_websocket_proxy.exe");

        STARTUPINFOW si;
        PROCESS_INFORMATION pi;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        ZeroMemory(&pi, sizeof(pi));

        if (!CreateProcessW(path.c_str(), NULL, NULL, NULL, FALSE, DETACHED_PROCESS, NULL, NULL, &si, &pi)) {
            brokerError("Failed to launch websocket_proxy. err=" + std::to_string(GetLastError()));
            return false;
        }
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        // wait for proxy to start up
        auto start = get_timestamp();
        while (!isProcessRunning(L"zorror_websocket_proxy.exe") && (get_timestamp() - start) < 10000) {
            std::this_thread::yield();
        }
    }

    uint32_t retry = 5;
    while (retry--) {
        try {
            // Waiting for the last queue in proxy server created
            server_queue_ = std::make_unique<SHM_QUEUE_T>(SERVER_TO_CLIENT_QUEUE);
            break;
        }
        catch (const std::runtime_error&) {
            broker_progress_(1);
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    if (!server_queue_) {
        brokerError("Failed to launch websocket_proxy. server_queue_ not ready");
        return false;
    }

    try {
        client_queue_ = std::make_unique<SHM_QUEUE_T>(CLIENT_TO_SERVER_QUEUE);
    }
    catch (const std::runtime_error&) {
        brokerError("Failed to launch websocket_proxy. client_req_queue not ready");
        return false;
    }

    server_queue_index_ = server_queue_->initial_reading_index();
    return true;
}

bool ZorroWebsocketProxyClient::_register() {
    auto [msg, index, size] = reserveMessage<RegisterMessage>();
    msg->type = Message::Type::Regster;
    auto reg = reinterpret_cast<RegisterMessage*>(msg->data);
    strcpy_s(reg->name, 32, name_.c_str());
    sendMessage(msg, index, size);
    if (!waitForResponse(msg, 20000)) {
        brokerError("Unable to connect to websocket_proxy. timeout");
        return false;
    }

    last_server_heartbeat_time_ = get_timestamp();
    if (msg->status.load(std::memory_order_relaxed) == Message::Status::FAILED) {
        brokerError(reg->err);
        return false;
    }
    server_pid_.store(reg->server_pid, std::memory_order_release);
    log_(L_INFO, "Proxy server connected, pid=" + std::to_string(reg->server_pid));
    return true;
}

void ZorroWebsocketProxyClient::unregister() {
    auto [msg, index, size] = reserveMessage();
    msg->type = Message::Type::Unregister;
    sendMessage(msg, index, size);
    server_pid_.store(0, std::memory_order_release);
    log_(L_INFO, "Unregistered, pid=" + std::to_string(pid_));
}

std::pair<uint32_t, bool> ZorroWebsocketProxyClient::openWs(const std::string& url) {
    if (url.size() > 255) {
        brokerError("URL is to long. limit is 255 character");
        return std::make_pair(0, false);
    }

    if (!server_pid_.load(std::memory_order_relaxed)) {
        broker_progress_(1);
        if (!connect()) {
            return std::make_pair(0, false);
        }
    }

    auto [msg, index, size] = reserveMessage<WsOpen>();
    msg->type = Message::Type::OpenWs;
    auto req = reinterpret_cast<WsOpen*>(msg->data);
    strcpy_s(req->url, 512, url.c_str());
    
    sendMessage(msg, index, size);

    if (!waitForResponse(msg)) {
        log_(L_DEBUG, "oepn ws timedout");
        return std::make_pair(0, false);
    }

    last_server_heartbeat_time_ = get_timestamp();
    if (msg->status.load(std::memory_order_relaxed) == Message::Status::FAILED) {
        brokerError(req->err);
        return std::make_pair(0, false);
    }
    
    log_(L_DEBUG, "ws connected. id=" + std::to_string(req->id) + ", new=" + std::to_string(req->new_connection));
    return std::make_pair(req->id, req->new_connection);
}

bool ZorroWebsocketProxyClient::closeWs(uint32_t id) {
    auto [msg, index, size] = reserveMessage<WsClose>();
    msg->type = Message::Type::CloseWs;
    auto req = reinterpret_cast<WsClose*>(msg->data);
    req->id = !id ? id_ : id;
    log_(L_INFO, "Close ws " + std::to_string(req->id));
    sendMessage(msg, index, size);
    return true;
}

void ZorroWebsocketProxyClient::send(uint32_t id, const char* data, size_t len) {
    auto [msg, index, size] = reserveMessage<WsRequest>(len);
    msg->type = Message::Type::WsRequest;
    auto req = reinterpret_cast<WsRequest*>(msg->data);
    req->len = len;
    req->id = id;
    memcpy(req->data, data, len);
    sendMessage(msg, index, size);
}

void ZorroWebsocketProxyClient::doWork() {
    while (run_.load(std::memory_order_relaxed)) {
        auto server_pid = server_pid_.load(std::memory_order_relaxed);
        if (!server_pid) {
            // not connected yet
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            continue;
        }
        auto now = get_timestamp();
        auto result = server_queue_->read(server_queue_index_);
        if (result.first) {
            auto msg = reinterpret_cast<Message*>(result.first);
            //log_(L_DEBUG, std::to_string(msg->type));
            last_server_heartbeat_time_ = now;
            if (server_pid != msg->pid) {
                continue;
            }
            switch (msg->type) {
            case Message::Type::OpenWs:
                handleWsOpen(msg);
                break;
            case Message::Type::CloseWs:
                handleWsClose(msg);
                break;
            case Message::Type::WsError:
                handleWsError(msg);
                break;
            case Message::Type::WsData:
                handleWsData(msg);
                break;
            }
        }

        bool heartbeat_sent = false;
        if (server_pid && (now - last_heartbeat_time_) > HEARTBEAT_INTERVAL) {
            auto [msg, index, size] = reserveMessage();
            msg->type = Message::Type::Heartbeat;
            sendMessage(msg, index, size);
            heartbeat_sent = true;
        }

        if (!result.first) {
            if (last_server_heartbeat_time_ && (now - last_server_heartbeat_time_) > HEARTBEAT_TIMEOUT) {
                log_(L_INFO, "Server " + std::to_string(server_pid) + " heatbeat timeout. now=" +
                    std::to_string(now) + ", last_seen=" + std::to_string(last_server_heartbeat_time_));
                server_pid_.store(0, std::memory_order_release);
                if (!websockets_.empty()) {
                    for (auto& id : websockets_) {
                        callback_->onWebsocketClosed(id);
                    }
                    websockets_.clear();
                }
            }
            else if (!heartbeat_sent) {
                std::this_thread::yield();
            }
        }
    }
}

void ZorroWebsocketProxyClient::handleWsOpen(Message* msg) {
    auto open = reinterpret_cast<WsOpen*>(msg->data);
    log_(L_DEBUG, "handleWsOPen, initiator=" + std::to_string(open->initiator));
    if (open->initiator == pid_) {
        websockets_.emplace(open->id);
        callback_->onWebsocketOpened(open->id);
    }
}

void ZorroWebsocketProxyClient::handleWsClose(Message* msg) {
    auto close = reinterpret_cast<WsClose*>(msg->data);
    auto it = websockets_.find(close->id);
    if (it != websockets_.end()) {
        websockets_.erase(close->id);
        callback_->onWebsocketClosed(close->id);
    }
    else {
        log_(L_DEBUG, "Ws closed. socket not found. id=" + std::to_string(close->id));
    }
}

void ZorroWebsocketProxyClient::handleWsError(Message* msg) {
    auto err = reinterpret_cast<WsError*>(msg->data);
    auto it = websockets_.find(err->id);
    if (it != websockets_.end()) {
        callback_->onWebsocketError(err->id, err->err, err->len);
    }
    else {
        std::string error;
        if (err->len) {
            error.append(err->err, err->len);
        }
        log_(L_DEBUG, "Ws error. socket not found. id=" + std::to_string(err->id) + " err=" + error);
    }
}

void ZorroWebsocketProxyClient::handleWsData(Message* msg) {
    auto data = reinterpret_cast<WsData*>(msg->data);
    auto it = websockets_.find(data->id);
    if (it != websockets_.end()) {
        callback_->onWebsocketData(data->id, data->data, data->len, data->remaining);
    }
    else {
        log_(L_DEBUG, "Ws data. socket not found. id=" + std::to_string(data->id));
    }
}