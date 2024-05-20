#include "zorro_websocket_proxy.h"
#include <thread>
#include <libwebsockets.h>
#include <fstream>
#include <iostream>
#include <cstdio>
#include <ctime>
#include <string>
#include "websocket.h"

#define SHM_OWNER TEXT("ZorroWebsocketProxy_shm_owner")

using namespace zorro::websocket;

namespace {
    uint32_t websocket_id_ = 0;
}

namespace {

    FILE* initLog() {
        std::string log_file = "./Log/WebsocketProxy.log";
        FILE* rt = fopen(log_file.c_str(), "a+");

        if (!rt) {
            // When the proxyed launched manually from websocket_proxy folder
            log_file = "../../Log/WebsocketProxy.log";
            rt = fopen(log_file.c_str(), "a+");
            if (!rt) {
                std::cout << "Failed to open log " << log_file << " " << errno << std::endl;
            }
        }
        return rt;
    }

    struct LogFile {
        FILE* file = nullptr;

        LogFile(FILE* f): file(f) {}
        ~LogFile() {
            if (file) {
                fclose(file);
                file = nullptr;
            }
        }
    };

    LogFile logfile(initLog());

    void log(int level, const char* line) {
        if (lwsl_visible(level) && logfile.file) {
            char buf[64];
            lwsl_timestamp(level, buf, 64);
            fprintf(logfile.file, "%s%s", buf, line);
            fflush(logfile.file);
        }
    }

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

ZorroWebsocketProxy::ZorroWebsocketProxy(uint32_t server_queue_size, int32_t logLevel)
    : client_queue_(1 << 16, CLIENT_TO_SERVER_QUEUE)
    , server_queue_(server_queue_size, SERVER_TO_CLIENT_QUEUE)
    , client_index_(client_queue_.initial_reading_index())
    , pid_(GetCurrentProcessId())
    , exec_path_(GetExePath())
    , closed_sockets_(256) {

    if (!logLevel || !logfile.file) {
        lws_set_log_level(LLL_ERR, nullptr);
    }
    else {
        lws_set_log_level(logLevel, log);
    }

    hMapFile_ = CreateFileMapping(
        INVALID_HANDLE_VALUE,   // use paging file
        NULL,                   // default security
        PAGE_READWRITE,         // read/write access
        0,                      // maximum object size (high-order DWORD)
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
        owner_pid_ = new (lpvMem_) std::atomic<DWORD>(pid_);
    }
    else {
        owner_pid_ = reinterpret_cast<std::atomic<DWORD>*>(lpvMem_);
    }
}

ZorroWebsocketProxy::~ZorroWebsocketProxy() {
    run_.store(false, std::memory_order_release);
    if (ws_thread_.joinable()) {
        ws_thread_.join();
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
    lwsl_user("WebsocketProxy destroyed\n");
}

void ZorroWebsocketProxy::run() {
    if (!own_shm_) {
        auto owner = owner_pid_->load(std::memory_order_relaxed);
        if (owner) {
            lwsl_user("Shm created by other ZorroWebsocketProxy instance. PID=%d\n", owner);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            if (isProcessRunning(owner)) {
                lwsl_user("Only one ZorroWebsocketProxy instance is allowed. Shutdown. PID=%d\n", pid_);
                exit(-1);
            }
        }

        while (!owner_pid_->compare_exchange_strong(owner, pid_, std::memory_order_release, std::memory_order_relaxed)) {
            if (owner != 0) {
                lwsl_user("PID %d take over the ownership. Shutdown\n", owner);
                exit(-1);
            }
        }

        lwsl_user("The other ZorroWebsocketProxy instance is dead, taking over ownership\n");
    }

    lwsl_user("\n\nZorroWebsocketProxy started. PID=%d\n\n", pid_);

    while (run_.load(std::memory_order_relaxed)) {
        auto req = client_queue_.read(client_index_);
        if (req.first) {
            handleClientMessage(reinterpret_cast<Message&>(*req.first));
        }

        removeClosedSockets();
        
        if (!checkHeartbeats() && !req.first) {
            std::this_thread::yield();
        }

        if (shutdown_time_ && (get_timestamp() - shutdown_time_) >= 10000) {
            run_.store(false, std::memory_order_release);
            break;
        }
    }

    lwsl_user("ZorroWebsocketProxy Exit. PID=%d\n", pid_);
}

void ZorroWebsocketProxy::handleClientMessage(Message& msg) {
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
        lwsl_user("Unknown msg, type=%d\n", msg.type);
        break;
    }
}

ZorroWebsocketProxy::ClientInfo* ZorroWebsocketProxy::getClient(DWORD pid) {
    auto it = clients_.find(pid);
    if (it != clients_.end()) {
        it->second.last_heartbeat_time = get_timestamp();
        return &it->second;
    }
    return nullptr;
}

void ZorroWebsocketProxy::handleClientRegistration(Message& msg) {
    auto reg = reinterpret_cast<RegisterMessage*>(msg.data);
    lwsl_user("Register client %d connected, name: %s\n", msg.pid, reg->name);
    reg->server_pid = pid_;
    shutdown_time_ = 0;

    auto it = clients_.find(msg.pid);
    if (it == clients_.end()) {
        it = clients_.emplace(msg.pid, ClientInfo()).first;
    }
    it->second.last_heartbeat_time = get_timestamp();
    msg.status.store(Message::Status::SUCCESS, std::memory_order_release);
}

void ZorroWebsocketProxy::unregisterClient(uint32_t pid) {
    auto it = clients_.find(pid);
    if (it != clients_.end()) {
        lwsl_user("Unregister client %d\n", pid);
        std::vector<uint32_t> to_close;
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
            lwsl_user("Last client disconnected. Shuting down...\n");
            shutdown_time_ = get_timestamp();
        }
    }
}

void ZorroWebsocketProxy::handleClientHeartbeat(Message& msg) {
    getClient(msg.pid);
}

void ZorroWebsocketProxy::openWs(Message& msg) {
    auto req = reinterpret_cast<WsOpen*>(msg.data);
    auto client = getClient(msg.pid);
    if (client) {
        auto it = websocketsByUrl_.find(req->url);
        if (it != websocketsByUrl_.end()) {
            auto& websocket = it->second;
            auto state = websocket->status_.load(std::memory_order_relaxed);
            if (state != Websocket::Status::DISCONNECTING && state != Websocket::Status::DISCONNECTED) {
                it->second->clients().emplace(msg.pid);
                auto id = websocket->id();
                req->id = id;
                req->new_connection = (state == Websocket::Status::CONNECTING);
                onWsOpened(id, msg.pid);
                lwsl_user("Websocket %s already opened. id=%d, new=%d, client=%d\n", req->url, id, req->new_connection, msg.pid);
                msg.status.store(Message::Status::SUCCESS, std::memory_order_release);
                return;
            }
        }
        
        openNewWs(msg, req);
    }
    else {
        snprintf(req->err, 256, "Client %d not found", msg.pid);
        msg.status.store(Message::Status::FAILED, std::memory_order_release);
    }
}

void ZorroWebsocketProxy::openNewWs(Message& msg, WsOpen* req) {
    lwsl_user("Opening ws %s, clinet=%d\n", req->url, msg.pid);
    req->new_connection = true;
    auto websocket = std::make_shared<Websocket>(this, pid_ * 10000 + (++websocket_id_), req->url);
    auto b = websocket->open(msg.pid);
    if (b) {
        req->id = websocket->id();
        websocket->clients().emplace(msg.pid);
        websocketsByUrl_.emplace(req->url, websocket);
        websocketsById_.emplace(websocket->id(), std::move(websocket));
    }
    msg.status.store(b ? Message::Status::SUCCESS : Message::Status::FAILED, std::memory_order_release);
}

void ZorroWebsocketProxy::closeWs(Message& msg) {
    auto req = reinterpret_cast<WsClose*>(msg.data);
    auto client = getClient(msg.pid);
    if (client) {
        closeWs(req->id, msg.pid);
    }
    msg.status.store(Message::Status::SUCCESS, std::memory_order_release);
}

void ZorroWebsocketProxy::closeWs(uint32_t id, DWORD pid) {
    lwsl_user("Close ws. id=%d, pid=%d\n", id, pid);
    auto it = websocketsById_.find(id);
    if (it != websocketsById_.end()) {
        auto& websocket = it->second;
        auto itr = websocket->clients_.find(pid);
        if (itr != websocket->clients_.end()) {
            websocket->clients_.erase(itr);
            lwsl_user("WS client %d removed from ws id=%d\n", pid, id);
            if (websocket->clients_.empty()) {
                lwsl_user("Close ws %s\n", it->second->url().c_str());
                it->second->status_ = Websocket::Status::DISCONNECTING;
                it->second->stop();
                websocketsByUrl_.erase(it->second->url());
                websocketsById_.erase(it);
            }
        }
    } 
    else {
        lwsl_user("Close ws. socket not found id=%d\n", id);
    }
}
void ZorroWebsocketProxy::handleSubscribe(Message& msg) {
    auto req = reinterpret_cast<WsSubscription*>(msg.data);
    auto client = getClient(msg.pid);
    if (client) {
        lwsl_user("Subscribe %s client=%d ws_id=%d\n", req->symbol, msg.pid, req->id);
        auto it = websocketsById_.find(req->id);
        if (it != websocketsById_.end()) {
            auto& subscriptions = it->second->subscriptions_;
            auto sub_it = subscriptions.find(req->symbol);
            if (sub_it == subscriptions.end()) {
                subscriptions.emplace(req->symbol, 1);
                if (it->second->send(req->request, req->request_len)) {
                    msg.status.store(Message::Status::SUCCESS, std::memory_order_release);
                    return;
                }
            }
            else {
                ++sub_it->second;
                req->existing = true;
                msg.status.store(Message::Status::SUCCESS, std::memory_order_release);
                return;
            }
        }
        else {
            lwsl_user("Websocket not found. id=\n", req->id);
        }
    }
    else {
        lwsl_user("Client not found. pid=\n", msg.pid);
    }
    msg.status.store(Message::Status::FAILED, std::memory_order_release);
}

void ZorroWebsocketProxy::handleUnsubscribe(Message& msg) {
    auto req = reinterpret_cast<WsSubscription*>(msg.data);
    auto client = getClient(msg.pid);
    if (client) {
        lwsl_user("Unsubscribe %s client=%d ws_id=%d\n", req->symbol, msg.pid, req->id);
        auto it = websocketsById_.find(req->id);
        if (it != websocketsById_.end()) {
            auto& subscriptions = it->second->subscriptions_;
            auto sub_it = subscriptions.find(req->symbol);
            if (sub_it != subscriptions.end()) {
                if (--sub_it->second == 0) {
                    subscriptions.erase(sub_it);
                    if (!it->second->send(req->request, req->request_len)) {
                        msg.status.store(Message::Status::FAILED, std::memory_order_release);
                        return;
                    }
                }
            }
            else {
                lwsl_user("Subscription not find. symbol=%s ws_id=%d\n", req->symbol, req->id);
            }
        }
        else {
            lwsl_user("Websocket not found. id=\n", req->id);
        }
    }
    else {
        lwsl_user("Client not found. pid=\n", msg.pid);
    }
    msg.status.store(Message::Status::SUCCESS, std::memory_order_release);
}

void ZorroWebsocketProxy::sendWsRequest(Message& msg) {
    auto req = reinterpret_cast<WsRequest*>(msg.data);
    auto client = getClient(msg.pid);
    if (client) {
        //lwsl_user("--> %.*s\n", req->len, req->data);
        auto it = websocketsById_.find(req->id);
        if (it != websocketsById_.end()) {
            if (it->second->send(req->data, req->len)) {
#ifdef _DEBUG
                lwsl_user("--> %.*s\n", req->len, req->data);
#endif
                msg.status.store(Message::Status::SUCCESS, std::memory_order_release);
                return;
            }
            onWsError(req->id, "Failed to send message. ", 23);
        }
        else {
            std::string err = "Failed to send message. Websocket not found. id=" + std::to_string(req->id);
            onWsError(req->id, err.c_str(), err.size());
        }
    }
    else {
        std::string err = "Failed to send message. Client not found. pid=" + std::to_string(msg.pid);
        onWsError(req->id, err.c_str(), err.size());
    }
    msg.status.store(Message::Status::FAILED, std::memory_order_release);
}

void ZorroWebsocketProxy::sendMessage(uint64_t index, uint32_t size) {
    sendMessage(index, size, get_timestamp());
}

void ZorroWebsocketProxy::sendMessage(uint64_t index, uint32_t size, uint64_t now) {
    server_queue_.publish(index, size);
    last_heartbeat_time_ = now;
}

bool ZorroWebsocketProxy::checkHeartbeats() {
    if (clients_.empty()) {
        return false;
    }
    auto now = get_timestamp();
    bool hasActivity = sendHeartbeat(now);

    for (auto it = clients_.begin(); it != clients_.end();) {
        auto& client = it->second;
        if ((now - client.last_heartbeat_time) > 30000) {
            lwsl_user("Client %d heartbeat lost\n", it->first);
            unregisterClient(it->second.pid);
            it = clients_.erase(it);

            if (clients_.empty()) {
                lwsl_user("Last client disconnected. Stop heartbeating...\n");
                shutdown_time_ = get_timestamp();
            }
        }
        else {
            ++it;
        }
    }

    return hasActivity;
}

bool ZorroWebsocketProxy::sendHeartbeat() {
    return sendHeartbeat(get_timestamp());
}

bool ZorroWebsocketProxy::sendHeartbeat(uint64_t now) {
    if ((now - last_heartbeat_time_) > HEARTBEAT_INTERVAL) {
        auto [msg, index, size] = reserveMessage();
        msg->type = Message::Type::Heartbeat;
        sendMessage(index, size, now);
        return true;
    }
    return false;
}

void ZorroWebsocketProxy::removeClosedSockets() {
    std::pair<uint32_t*, size_t> read;
    while ((read = closed_sockets_.read(closed_sockets_index_)).first) {
        auto it = websocketsById_.find(*read.first);
        if (it != websocketsById_.end()) {
            lwsl_user("Remove websocket id=%d\n", it->first);
            websocketsByUrl_.erase(it->second->url());
            websocketsById_.erase(it);
        }
    }
}


// Websocket Callbacks call from socket service thread

void ZorroWebsocketProxy::onWsOpened(uint32_t id, DWORD initiator) {
    auto [msg, index, size] = reserveMessage<WsOpen>();
    msg->type = Message::Type::OpenWs;
    auto open = reinterpret_cast<WsOpen*>(msg->data);
    open->id = id;
    open->initiator = initiator;
    open->new_connection = true;
    sendMessage(index, size);
}

void ZorroWebsocketProxy::onWsClosed(uint32_t id) {
    auto [msg, index, size] = reserveMessage<WsClose>();
    msg->type = Message::Type::CloseWs;
    auto wsclose = reinterpret_cast<WsClose*>(msg->data);
    wsclose->id = id;
    sendMessage(index, size);

    auto idx = closed_sockets_.reserve();
    (*closed_sockets_[idx]) = id;
    closed_sockets_.publish(idx);

    lwsl_user("ws %d closed\n", id);
}

void ZorroWebsocketProxy::onWsError(uint32_t id, const char* err, size_t len) {
    auto [msg, index, size] = reserveMessage<WsError>(len);
    msg->type = Message::Type::WsError;
    auto e = reinterpret_cast<WsError*>(msg->data);
    e->id = id;
    e->len = len;
    if (err && len) {
        memcpy(e->err, err, len);
    }
    sendMessage(index, size);
}

void ZorroWebsocketProxy::onWsData(uint32_t id, const char* data, size_t len, size_t remaining) {
    auto [msg, index, size] = reserveMessage<WsData>(len);
    msg->type = Message::Type::WsData;
    auto d = reinterpret_cast<WsData*>(msg->data);
    d->id = id;
    d->len = len;
    d->remaining = remaining;
    if (data && len) {
        memcpy(d->data, data, len);
    }
    sendMessage(index, size);
#ifdef _DEBUG
    lwsl_user("<-- %.*s\n", len, data);
#endif
}