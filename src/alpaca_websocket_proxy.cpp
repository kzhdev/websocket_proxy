#include <boost/asio.hpp>
#include "alpaca_websocket_proxy.h"
#include <thread>
#include <fstream>
#include <iostream>
#include <cstdio>
#include <ctime>
#include <string>
#include <format>
#include <spdlog/spdlog.h>
#include "websocket.h"
#include <alpaca_websocket_proxy/alpaca_websocket_proxy_client.h>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/spawn.hpp>

#define SHM_OWNER TEXT("AlpacaWebsocketProxy_shm_owner")

using namespace alpaca_websocket_proxy;

namespace {
    uint32_t websocket_id_ = 0;
}

namespace {
    std::string GetExePath()
    {
#ifdef _WIN32
        wchar_t result[MAX_PATH] = { 0 };
        GetModuleFileNameW(NULL, result, MAX_PATH);
        std::wstring wsPath(result);
        int count = WideCharToMultiByte(CP_ACP, 0, wsPath.c_str(), wsPath.length(), NULL, 0, NULL, NULL);
        std::string path(count, 0);
        WideCharToMultiByte(CP_ACP, 0, wsPath.c_str(), -1, &path[0], count, NULL, NULL);
        auto pos = path.rfind("\\");
#else
        char result[PATH_MAX];
        ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
        std::string path(result, (count > 0) ? count : 0);
        auto pos = path.rfind("/");
#endif
        return path.substr(0, pos);
    }
}

AlpacaWebsocketProxy::AlpacaWebsocketProxy(uint32_t server_queue_size)
    : client_queue_(1 << 16, CLIENT_TO_SERVER_QUEUE)
    , server_queue_(server_queue_size, SERVER_TO_CLIENT_QUEUE)
    , client_index_(client_queue_.initial_reading_index())
    , pid_(GetCurrentProcessId())
    , exec_path_(GetExePath())
    , closed_sockets_(256) {

    hMapFile_ = CreateFileMapping(
        INVALID_HANDLE_VALUE,   // use paging file
        NULL,                   // default security
        PAGE_READWRITE,         // read/write access
        0,                      // maximum object size (high-order DWORD)f
        sizeof(DWORD),          // maximum object size (low-order DWORD)
        SHM_OWNER               // name of mapping object
    );

    if (hMapFile_ == NULL) {
        throw std::runtime_error("Failed to create shm. err=" + std::to_string(GetLastError()));
    }

    if (GetLastError() != ERROR_ALREADY_EXISTS) {
        own_shm_ = true;
    }

    lpvMem_ = MapViewOfFile(hMapFile_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(DWORD));
    if (!lpvMem_) {
        throw std::runtime_error("Failed to create shm. err=" + std::to_string(GetLastError()));
    }

    if (own_shm_) {
        owner_pid_ = new (lpvMem_) std::atomic<uint64_t>(pid_);
    }
    else {
        owner_pid_ = reinterpret_cast<std::atomic<uint64_t>*>(lpvMem_);
    }
}

AlpacaWebsocketProxy::~AlpacaWebsocketProxy() {
    run_.store(false, std::memory_order_release);
    // if (ws_thread_.joinable()) {
    //     ws_thread_.join();
    // }
    if (!ioc_.stopped())
    {
        ioc_.stop();
    }

    if (lpvMem_) {
        if (own_shm_) {
            owner_pid_->store(0, std::memory_order_release);
        }
        UnmapViewOfFile(lpvMem_);
        lpvMem_ = nullptr;
    }

    if (hMapFile_) {
        CloseHandle(hMapFile_);
        hMapFile_ = nullptr;
    }
}

void AlpacaWebsocketProxy::run() {
    if (!own_shm_) {
        auto owner = owner_pid_->load(std::memory_order_relaxed);
        if (owner) {
            SPDLOG_INFO("Shm created by other AlpacaWebsocketProxy instance. PID={}", owner);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            if (isProcessRunning(owner)) {
                SPDLOG_INFO("Only one AlpacaWebsocketProxy instance is allowed. Shutdown. PID={}", pid_);
                exit(-1);
            }
        }

        while (!owner_pid_->compare_exchange_strong(owner, pid_, std::memory_order_release, std::memory_order_relaxed)) {
            if (owner != 0) {
                SPDLOG_INFO("PID {} take over the ownership. Shutdown", owner);
                exit(-1);
            }
        }

        SPDLOG_INFO("The other AlpacaWebsocketProxy instance is dead, taking over ownership");
    }

    boost::asio::signal_set signals(ioc_, SIGINT, SIGTERM);
    signals.async_wait([&](auto, auto){ stop(); });

    SPDLOG_INFO("\n\nWebsocketProxy started. PID={}\n", pid_);

    // start heartbeat
    ioc_.post([this]() {
        // send a heartbeat to notify client
        // server is up running
        auto now = get_timestamp();
        sendHeartbeat(now);
        
        startHeartbeat(); 
    });
    
    // start process incoming client messages
    ioc_.post([this]() { processClientMessage(); });

    while (run_.load(std::memory_order_relaxed))
    {
        try
        {
            if (ioc_.stopped())
            {
                ioc_.restart();
            }
            boost::system::error_code ec;
            ioc_.run(ec);
            SPDLOG_INFO("asio loop stopped");
        }
        catch(const std::exception& e)
        {
            ioc_.restart();
            SPDLOG_ERROR(std::format("{}", e.what()));
        }
    }

    SPDLOG_INFO("AlpacaWebsocketProxy Exit. PID={}", pid_);
}

void AlpacaWebsocketProxy::startHeartbeat() {
    checkHeartbeats();
    if (run_.load(std::memory_order_relaxed)) {
        ioc_.post([this]() { startHeartbeat(); });
    }
}

void AlpacaWebsocketProxy::processClientMessage() {
    auto req = client_queue_.read(client_index_);
    if (req.first) {
        handleClientMessage(reinterpret_cast<Message&>(*req.first));
    }

    removeClosedSockets();

    if (run_.load(std::memory_order_relaxed)) {
        if (shutdown_time_ && (get_timestamp() - shutdown_time_) >= 60000) {
            SPDLOG_INFO("Shuting down...");
            stop();
        }
        else
        {
            ioc_.post([this]() { processClientMessage(); });
        }
    }
}

void AlpacaWebsocketProxy::handleClientMessage(Message& msg) {
    switch (msg.type) {
    case Message::Type::Regster:
        handleClientRegistration(msg);
        break;
    case Message::Type::Unregister:
        unregisterClient(msg.pid);
        break;
    case Message::Type::Heartbeat:
        handleClientHeartbeat(msg);
        break;
    case Message::Type::OpenWs:
        openWs(msg);
        break;
    case Message::Type::CloseWs:
        closeWs(msg);
        break;
    case Message::Type::WsRequest:
        sendWsRequest(msg);
        break;
    case Message::Type::Subscribe:
        handleSubscribe(msg);
        break;
    case Message::Type::Unsubscribe:
        handleUnsubscribe(msg);
        break;
    case Message::Type::WsData:
    case Message::Type::WsError:
        break;
    default:
        SPDLOG_ERROR("Unknown msg, type={}", static_cast<uint32_t>(msg.type));
        break;
    }
}

AlpacaWebsocketProxy::ClientInfo* AlpacaWebsocketProxy::getClient(uint64_t pid) {
    auto it = clients_.find(pid);
    if (it != clients_.end()) {
        it->second.last_heartbeat_time = get_timestamp();
        return &it->second;
    }
    return nullptr;
}

void AlpacaWebsocketProxy::handleClientRegistration(Message& msg) {
    auto reg = reinterpret_cast<RegisterMessage*>(msg.data);
    SPDLOG_INFO("Register client {} connected, name: {}", msg.pid, reg->name);
    reg->server_pid = pid_;
    shutdown_time_ = 0;

    auto it = clients_.find(msg.pid);
    if (it == clients_.end()) {
        it = clients_.emplace(msg.pid, ClientInfo()).first;
    }
    it->second.last_heartbeat_time = get_timestamp();
    msg.status.store(Message::Status::SUCCESS, std::memory_order_release);
}

void AlpacaWebsocketProxy::unregisterClient(uint64_t pid) {
    auto it = clients_.find(pid);
    if (it != clients_.end()) {
        SPDLOG_INFO("Unregister client {}", pid);
        std::vector<uint64_t> to_close;
        to_close.reserve(websocketsById_.size());
        for (auto& kvp : websocketsById_) {
            auto& ws_clients = kvp.second->clients();
            auto itr = ws_clients.find(pid);
            if (itr != ws_clients.end()) {
                ws_clients.erase(itr);
                if (ws_clients.empty()) {
                    to_close.emplace_back(kvp.first);
                }
            }
        }
        for (auto id : to_close) {
            closeWs(id, pid);
        }
        clients_.erase(it);

        if (clients_.empty()) {
            SPDLOG_INFO("Last client disconnected.");
            shutdown_time_ = get_timestamp();
        }
    }
}

void AlpacaWebsocketProxy::handleClientHeartbeat(Message& msg) {
    getClient(msg.pid);
}

void AlpacaWebsocketProxy::openWs(Message& msg) {
    auto req = reinterpret_cast<WsOpen*>(msg.data);
    auto client = getClient(msg.pid);
    if (client) {
        auto it = websocketsByUrlApiKey_.find(WebsocketKey{req->url, req->api_key});
        if (it != websocketsByUrlApiKey_.end()) {
            auto& websocket = it->second;
            auto state = websocket->status_.load(std::memory_order_relaxed);
            if (state != Websocket::Status::DISCONNECTING && state != Websocket::Status::DISCONNECTED) {
                it->second->clients().emplace(msg.pid);
                auto id = websocket->id();
                req->id = id;
                req->client_pid = msg.pid;
                req->new_connection = (state == Websocket::Status::CONNECTING);
                onWsOpened(id, msg.pid);
                SPDLOG_INFO("Websocket {} already opened. id={}, new={}, client={}", req->url, id, req->new_connection, msg.pid);
                msg.status.store(Message::Status::SUCCESS, std::memory_order_release);
                return;
            }
        }
        
        openNewWs(msg, req);
    }
    else {
        snprintf(req->err, 256, "Client %llu not found", msg.pid);
        msg.status.store(Message::Status::FAILED, std::memory_order_release);
    }
}

void AlpacaWebsocketProxy::openNewWs(Message& msg, WsOpen* req) {
    SPDLOG_INFO("Opening ws {}, clinet={}", req->url, msg.pid);
    req->new_connection = true;
    auto websocket = std::make_shared<Websocket>(this, ioc_, ctx_, pid_ * 10000 + (++websocket_id_), req->url, req->api_key);
    // auto start = get_timestamp();
    asio::spawn(
        ioc_,
        std::bind(&Websocket::open, websocket, [this, websocket, &msg, req](bool success) {
            if (success) {
                onWsOpened(websocket->id(), msg.pid);
                req->id = websocket->id();
                req->client_pid = msg.pid;
                websocket->clients().emplace(msg.pid);
                websocketsByUrlApiKey_.emplace(WebsocketKey(req->url, req->api_key), websocket);
                websocketsById_.emplace(websocket->id(), std::move(websocket));
                msg.status.store(Message::Status::SUCCESS, std::memory_order_release);
            } else {
                msg.status.store(Message::Status::FAILED, std::memory_order_release);
            }
        }, std::placeholders::_1),
        // on completion, spawn will call this function
        [&msg](std::exception_ptr ex) {
            // if an exception occurred in the coroutine,
            // it's something critical, e.g. out of memory
            // we capture normal errors in the ec
            // so we just rethrow the exception here,
            // which will cause `ioc.run()` to throw
            if (ex) {
                SPDLOG_INFO("Open Failed......");
                msg.status.store(Message::Status::FAILED, std::memory_order_release);
                std::rethrow_exception(ex);
            }
        });
}

void AlpacaWebsocketProxy::closeWs(Message& msg) {
    auto req = reinterpret_cast<WsClose*>(msg.data);
    auto client = getClient(msg.pid);
    if (client) {
        closeWs(req->id, msg.pid);
    }
    msg.status.store(Message::Status::SUCCESS, std::memory_order_release);
}

void AlpacaWebsocketProxy::closeWs(uint64_t id, uint64_t pid) {
    SPDLOG_INFO("Close ws. id={}, pid={}", id, pid);
    auto it = websocketsById_.find(id);
    if (it != websocketsById_.end()) {
        auto& websocket = it->second;
        auto itr = websocket->clients_.find(pid);
        if (itr != websocket->clients_.end()) {
            websocket->clients_.erase(itr);
            SPDLOG_INFO("WS client {} removed from ws id={}", pid, id);
            if (websocket->clients_.empty()) {
                SPDLOG_INFO("Close ws {}", websocket->url_.c_str());
                it->second->close();
                websocketsByUrlApiKey_.erase(WebsocketKey(websocket->url_, websocket->api_key_));
                websocketsById_.erase(it);
            }
        }
    } 
    else {
        SPDLOG_DEBUG("Close ws. socket not found id={}", id);
    }
}
void AlpacaWebsocketProxy::handleSubscribe(Message& msg) {
    auto req = reinterpret_cast<WsSubscription*>(msg.data);
    auto client = getClient(msg.pid);
    if (client) {
        SPDLOG_INFO("Subscribe {} client={} ws_id={} type={}", req->symbol, msg.pid, req->id, (int)req->type);
        auto it = websocketsById_.find(req->id);
        if (it != websocketsById_.end()) {
            auto& subscriptions = it->second->subscriptions_;
            auto sub_it = subscriptions.find(req->symbol);
            if (sub_it == subscriptions.end()) {
                auto [sub_it, b] = subscriptions.emplace(req->symbol, req->type);
                sub_it->second.clients_.emplace(msg.pid);
                it->second->send(req->request, req->request_len);
                msg.status.store(Message::Status::SUCCESS, std::memory_order_release);
                return;
            }
            else {
                sub_it->second.clients_.emplace(msg.pid);
                if (!(sub_it->second.type_ & req->type))
                {
                    it->second->send(req->request, req->request_len);
                    sub_it->second.type_ |= req->type;
                }
                req->existing = true;
                msg.status.store(Message::Status::SUCCESS, std::memory_order_release);
                return;
            }
        }
        else {
            SPDLOG_DEBUG("Websocket not found. id={}", req->id);
        }
    }
    else {
        SPDLOG_DEBUG("Client not found. pid={}", msg.pid);
    }
    msg.status.store(Message::Status::FAILED, std::memory_order_release);
}

void AlpacaWebsocketProxy::handleUnsubscribe(Message& msg) {
    auto req = reinterpret_cast<WsSubscription*>(msg.data);
    auto client = getClient(msg.pid);
    if (client) {
        SPDLOG_INFO("Unsubscribe {} client={} ws_id={}", req->symbol, msg.pid, req->id);
        auto it = websocketsById_.find(req->id);
        if (it != websocketsById_.end()) {
            auto& subscriptions = it->second->subscriptions_;
            auto sub_it = subscriptions.find(req->symbol);
            if (sub_it != subscriptions.end()) {
                sub_it->second.clients_.erase(msg.pid);
                if (sub_it->second.clients_.empty()) {
                    
                    subscriptions.erase(sub_it);
                    it->second->send(req->request, req->request_len);
                }
            }
            else {
                SPDLOG_DEBUG("Subscription not find. symbol={} ws_id={}", req->symbol, req->id);
            }
        }
        else {
            SPDLOG_DEBUG("Websocket not found. id={}", req->id);
        }
    }
    else {
        SPDLOG_DEBUG("Client not found. pid={}", msg.pid);
    }
    msg.status.store(Message::Status::SUCCESS, std::memory_order_release);
}

void AlpacaWebsocketProxy::sendWsRequest(Message& msg) {
    auto req = reinterpret_cast<WsRequest*>(msg.data);
    auto client = getClient(msg.pid);
    if (client) {
        auto it = websocketsById_.find(req->id);
        if (it != websocketsById_.end()) {
            it->second->send(req->data, req->len);
            msg.status.store(Message::Status::SUCCESS, std::memory_order_release);
            return;
        }
        else {
            std::string err = std::format("Failed to send message. Websocket not found. id={}", req->id);
            onWsError(req->id, err.c_str(), err.size());
        }
    }
    else {
        std::string err = std::format("Failed to send message. Client not found. pid={}", msg.pid);
        onWsError(req->id, err.c_str(), err.size());
    }
    msg.status.store(Message::Status::FAILED, std::memory_order_release);
}

void AlpacaWebsocketProxy::sendMessageToClient(uint64_t index, uint32_t size) {
    sendMessageToClient(index, size, get_timestamp());
}

void AlpacaWebsocketProxy::sendMessageToClient(uint64_t index, uint32_t size, uint64_t now) {
    server_queue_.publish(index, size);
    last_heartbeat_time_ = now;
}

bool AlpacaWebsocketProxy::checkHeartbeats() {
    if (clients_.empty()) {
        return false;
    }
    auto now = get_timestamp();
    bool hasActivity = sendHeartbeat(now);

    for (auto it = clients_.begin(); it != clients_.end();) {
        auto& client = it->second;
        if ((now - client.last_heartbeat_time) > 30000) {
            SPDLOG_INFO("Client {} heartbeat lost", it->first);
            unregisterClient(it->second.pid);
            it = clients_.erase(it);

            if (clients_.empty()) {
                SPDLOG_INFO("Last client disconnected.");
                shutdown_time_ = get_timestamp();
            }
        }
        else {
            ++it;
        }
    }

    return hasActivity;
}

bool AlpacaWebsocketProxy::sendHeartbeat() {
    return sendHeartbeat(get_timestamp());
}

bool AlpacaWebsocketProxy::sendHeartbeat(uint64_t now) {
    if ((now - last_heartbeat_time_) > HEARTBEAT_INTERVAL) {
        auto [msg, index, size] = reserveMessage();
        msg->type = Message::Type::Heartbeat;
        sendMessageToClient(index, size, now);
        return true;
    }
    return false;
}

void AlpacaWebsocketProxy::removeClosedSockets() {
    std::pair<uint64_t*, size_t> read;
    while ((read = closed_sockets_.read(closed_sockets_index_)).first) {
        auto it = websocketsById_.find(*read.first);
        if (it != websocketsById_.end()) {
            SPDLOG_INFO("Remove websocket id={}", it->first);
            websocketsByUrlApiKey_.erase(WebsocketKey(it->second->url_, it->second->api_key_));
            websocketsById_.erase(it);
        }
    }
}


// Websocket Callbacks
void AlpacaWebsocketProxy::onWsOpened(uint64_t id, uint64_t client_pid) {
    auto [msg, index, size] = reserveMessage<WsOpen>();
    msg->type = Message::Type::OpenWs;
    auto open = reinterpret_cast<WsOpen*>(msg->data);
    open->id = id;
    open->client_pid = client_pid;
    open->new_connection = true;
    sendMessageToClient(index, size);
}

void AlpacaWebsocketProxy::onWsClosed(uint64_t id) {
    auto [msg, index, size] = reserveMessage<WsClose>();
    msg->type = Message::Type::CloseWs;
    auto wsclose = reinterpret_cast<WsClose*>(msg->data);
    wsclose->id = id;
    sendMessageToClient(index, size);

    auto idx = closed_sockets_.reserve();
    (*closed_sockets_[idx]) = id;
    closed_sockets_.publish(idx);

    SPDLOG_INFO("Ws {} closed", id);
}

void AlpacaWebsocketProxy::onWsError(uint64_t id, const char* err, uint32_t len) {
    auto [msg, index, size] = reserveMessage<WsError>(len);
    msg->type = Message::Type::WsError;
    auto e = reinterpret_cast<WsError*>(msg->data);
    e->id = id;
    e->len = len;
    if (err && len) {
        memcpy(e->err, err, len);
    }
    sendMessageToClient(index, size);
}

void AlpacaWebsocketProxy::onWsData(uint64_t id, const char* data, uint32_t len, uint32_t remaining) {
    auto [msg, index, size] = reserveMessage<WsData>(len);
    msg->type = Message::Type::WsData;
    auto d = reinterpret_cast<WsData*>(msg->data);
    d->id = id;
    d->len = len;
    d->remaining = remaining;
    if (data && len) {
        memcpy(d->data, data, len);
    }
    sendMessageToClient(index, size);
}