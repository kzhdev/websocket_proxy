#pragma once

#include <websocket_proxy/websocket_proxy_client.h>
#include <nlohmann/json.hpp>
#include <string>
#include <iostream>
#include <sstream>
#include <atomic>

namespace alpaca_websocket {

using WebSocketProxyClient = websocket_proxy::WebsocketProxyClient;

class AlpacaWebSocketClient : public WebSocketProxyClient, public websocket_proxy::WebsocketProxyCallback
{
    uint64_t ws_id_ = 0;
    std::string url_;
    std::string api_key_;
    std::string api_secret_;
    std::stringstream ss_;
    std::atomic_bool authenticated_;

    enum RequestStatus : uint8_t
    {
        None,
        WaitingForResult,
        Completed,
        Failed,
    };
    std::atomic<RequestStatus> request_status_{RequestStatus::None};

public:
    AlpacaWebSocketClient(std::string &&proxy_exe_path)
        : WebSocketProxyClient(this, "AlpacaWebSocketClient", std::move(proxy_exe_path))
    {}

    ~AlpacaWebSocketClient() override
    {
        close();
    }

    bool open(std::string &&url, std::string &&api_key, std::string &&api_secret)
    {
        url_ = std::move(url);
        api_key_ = std::move(api_key);
        api_secret_ = std::move(api_secret);

        request_status_.store(RequestStatus::WaitingForResult, std::memory_order_relaxed);
        auto [ws_id, is_new_connection] = openWebSocket(url_, api_key_);
        if (!ws_id)
        {
            return false;
        }

        if (!is_new_connection)
        {
            ws_id_ = ws_id;
            request_status_.store(RequestStatus::None, std::memory_order_relaxed);
            std::cout << url_ << " key=" << api_key_ << " already connected" << std::endl;
            return true;
        }

        RequestStatus status;
        while((status = request_status_.load(std::memory_order_relaxed)) == RequestStatus::WaitingForResult)
        {
            std::this_thread::yield();
        }
        return status == RequestStatus::Completed;
    }

    void close()
    {
        if (ws_id_)
        {
            closeWebSocket(ws_id_);
        }
    }

    bool subscribe(const std::string &symbol)
    {
        std::cout << "subscribe " << symbol << std::endl;

        nlohmann::json j;
        j["action"] = "subscribe";
        j["trades"] = nlohmann::json::array({symbol});
        j["quotes"] = nlohmann::json::array({symbol});
        auto str = j.dump();

        request_status_.store(RequestStatus::WaitingForResult, std::memory_order_relaxed);
        send(ws_id_, str.c_str(), (uint32_t)str.size());

        RequestStatus status;
        while(!(status = request_status_.load(std::memory_order_relaxed)) == RequestStatus::WaitingForResult)
        {
            std::this_thread::yield();
        }
        return status == RequestStatus::Completed;
    }

    void onWebsocketProxyServerDisconnected() override
    {
        std::cout << "WebSocket Proxy Server disconnected" << std::endl;
        if (ws_id_)
        {
            closeWebSocket(ws_id_);
        }
    }
    
    void onWebsocketOpened(uint64_t id) override
    {
        std::cout << "WebSocket Opened. id=" << id << std::endl;
        ws_id_ = id;
    }

    void onWebsocketClosed(uint64_t id) override
    {
        std::cout << "WebSocket closed. id=" << id << std::endl;
    }

    void onWebsocketError(uint64_t id, const char* err, uint32_t len) override 
    {
        std::cout << "WebSocket id=" << id << " Error: " << std::string(err, len) << std::endl;
        closeWebSocket(ws_id_);
    }

    void onWebsocketData(uint64_t id, const char* data, uint32_t len, uint32_t remaining) override
    {
        if (data && len)
        {
            ss_ << std::string(data, len);
        }

        if (remaining == 0) 
        { // message complete
            try
            {
                nlohmann::json j = nlohmann::json::parse(ss_.str());
                if (j.is_array())
                {
                    for (auto& item : j.items()) 
                    {
                        auto &obj = item.value();
                        if (!obj.is_object())
                        {
                            assert(false);
                            continue;
                        }

                        if (obj.contains("T"))
                        {
                            const auto& type = obj["T"];
                            if (type == "error")
                            {
                                std::cout << "On Ws error" << obj["msg"] << '('<< obj["code"] << ')' << std::endl;
                                if (request_status_.load(std::memory_order_relaxed) == RequestStatus::WaitingForResult)
                                {
                                    request_status_.store(RequestStatus::Failed, std::memory_order_relaxed);
                                }
                                closeWebSocket(id);
                                break;
                            }

                            if (type == "success")
                            {
                                auto &msg = obj["msg"].get_ref<const std::string&>();
                                if (msg == "connected")
                                {
                                    authenticate();
                                }
                                else if (msg == "authenticated")
                                {
                                    std::cout << "Authenticated" << std::endl;
                                    if (request_status_.load(std::memory_order_relaxed) == RequestStatus::WaitingForResult)
                                    {
                                        request_status_.store(RequestStatus::Completed, std::memory_order_relaxed);
                                    }
                                }
                                else
                                {
                                    std::cout << ss_.str() << std::endl;
                                }
                            }
                            else if (type == "subscription")
                            {
                                // subscription
                                std::cout << "subscription: " << ss_.str() << std::endl;

                                // TODO: when multiple clients subscribe at the same time, it's better to check the symbol
                                if (request_status_.load(std::memory_order_relaxed) == RequestStatus::WaitingForResult)
                                {
                                    request_status_.store(RequestStatus::Failed, std::memory_order_relaxed);
                                }
                            }
                            else if (type == "t")
                            {
                                // trade
                                std::cout << "Trade: " << ss_.str() << std::endl;
                            }
                            else if (type == "q")
                            {
                                // quote
                                std::cout << "Quote: " << ss_.str() << std::endl;
                            }
                        }
                    }
                }
            }
            catch(const std::exception& e)
            {
                // When mutile instances connected to the proxy, a new instance 
                // might reading a partial message from the queue. Drop the invalid message
            }

            // reset message buffer
            ss_.str("");
        }
    }

    void logError(std::function<std::string()>&& msg) override 
    {
        std::cout << "ERROR: " << msg() << std::endl;
    }

    void logWarning(std::function<std::string()>&& msg) override
    {
        std::cout << "WAR: " << msg() << std::endl;
    }
    void logInfo(std::function<std::string()>&& msg) override
    {
        std::cout << "INFO: " << msg() << std::endl;
    }
    void logDebug(std::function<std::string()>&& msg)
    {
        std::cout << "DEBUG: " << msg() << std::endl;
    }

private:
    void authenticate()
    {
        nlohmann::json j;
        j["action"] = "auth";
        j["key"] = api_key_;
        j["secret"] = api_secret_;
        std::cout << "Authenticating..." << std::endl;
        auto str = j.dump();
        send(ws_id_, str.c_str(), (uint32_t)str.size());
    }
};

}