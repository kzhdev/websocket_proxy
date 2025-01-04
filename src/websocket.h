// MIT License
// 
// Copyright (c) 2024-2025 Kun Zhao
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include "slick_queue.h"
#include "websocket_proxy.h"
#include <unordered_set>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/spawn.hpp>
#include <cstdlib>
#include "spdlog_include.h"
#include <atomic>
#include <winrt/Windows.Foundation.h>

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace websocket = beast::websocket; // from <boost/beast/websocket.hpp>
namespace asio = boost::asio;           // from <boost/asio.hpp>
namespace ssl = boost::asio::ssl;       // from <boost/asio/ssl.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>
using asio::awaitable;
using asio::use_awaitable;
using asio::co_spawn;
using asio::detached;

namespace websocket_proxy {

class Websocket : public std::enable_shared_from_this<Websocket>
{
    friend class WebsocketProxy;

    asio::io_context& ioc_;
    ssl::context& ctx_;
    WebsocketProxy* proxy_ = nullptr;
    tcp::resolver resolver_;
    websocket::stream<ssl::stream<beast::tcp_stream>> ws_;
    beast::flat_buffer r_buffer_;
    beast::multi_buffer w_buffer_;
    std::string url_;
    std::string api_key_;
    std::string host_;
    std::string path_;
    uint16_t port_ = -1;
    uint64_t id_ = 0;

    std::unordered_set<uint64_t> clients_;

    struct Subscription
    {
        uint8_t type_ = SubscriptionType::None;
        std::unordered_set<uint64_t> clients_;

        Subscription() = default;
        Subscription(SubscriptionType type) : type_(type) {}
    };
    std::unordered_map<std::string, Subscription> subscriptions_;

    enum Status : uint8_t 
    {
        CONNECTING,
        CONNECTED,
        DISCONNECTING,
        DISCONNECTED,
    };
    std::atomic<Status> status_{ Status::DISCONNECTED };
    
public:
    // Resolver and socket require an io_context
    explicit Websocket(WebsocketProxy* proxy, asio::io_context& ioc, ssl::context& ctx, uint64_t id, std::string url, std::string api_key)
        : ioc_(ioc)
        , ctx_(ctx)
        , proxy_(proxy)
        , resolver_(asio::make_strand(ioc))
        , ws_(asio::make_strand(ioc), ctx)
        , url_(std::move(url))
        , api_key_(std::move(api_key))
        , id_(id)
    {
        clients_.reserve(128);

        std::string protoco("wss");
        auto pos = url_.find("://");
        if (pos == std::string::npos)
        {
            pos = url_.find("/");
            if (pos == std::string::npos)
            {
                host_ = url_;
                path_ = "/";
            }
            else
            {
                host_ = url_.substr(0, pos);
                path_ = url_.substr(pos);
            }
        }
        else
        {
            protoco = url_.substr(0, pos);
            auto host_begin = pos + 3;
            auto pos1 = url_.find("/", host_begin);
            if (pos1 == std::string::npos)
            {
                host_ = url_.substr(host_begin);
                path_ = "/";
            }
            else
            {
                host_ = url_.substr(host_begin, pos1 - host_begin);
                path_ = url_.substr(pos1);
            }
        }
        
        pos = host_.find(':');
        if (pos != 3 && pos != 4 && pos != std::string::npos)
        {
            port_ = std::stoi(host_.substr(pos + 1));
            host_ = host_.substr(0, pos);
        }

        if (port_ == (uint16_t)-1)
        {
            port_ = (protoco == "ws") ? 80 : 443;
        }
    }

    ~Websocket() = default;

    uint32_t id() const noexcept { return id_; }

    std::unordered_set<uint64_t>& clients() noexcept { return clients_; }

    // Start the asynchronous operation
    void open(std::function<void(bool)> &&callback, asio::yield_context yield)
    {
        SPDLOG_INFO("Connecting to {}:{}...", host_, port_);
        status_.store(Status::CONNECTING, std::memory_order_release);

        beast::error_code ec;
        auto const result = resolver_.async_resolve(host_, std::to_string(port_), yield[ec]);
        if (ec) {
            return fail(ec, "resolve", &callback);
        }

        // Set a timeout on the operation
        beast::get_lowest_layer(ws_).expires_after(std::chrono::seconds(30));

        // Make the connection on the IP address we get from a lookup
        auto ep = beast::get_lowest_layer(ws_).async_connect(result, yield[ec]);
        if (ec) {
            return fail(ec, "connect", &callback);
        }

        // Set SNI Hostname (many hosts need this to handshake successfully)
        if(!SSL_set_tlsext_host_name(ws_.next_layer().native_handle(), host_.c_str()))
        {
            auto ec = beast::error_code(static_cast<int>(::ERR_get_error()), asio::error::get_ssl_category());
            return fail(ec, "connect", &callback);
        }

        // Update the host string. This will provide the value of the
        // Host HTTP header during the WebSocket handshake.
        // See https://tools.ietf.org/html/rfc7230#section-5.4
        host_ += ':' + std::to_string(ep.port());

        // Set a timeout on the operation
        beast::get_lowest_layer(ws_).expires_after(std::chrono::seconds(30));

        // Perform the SSL handshake
        ws_.next_layer().async_handshake(ssl::stream_base::client, yield[ec]);
        if (ec) {
            return fail(ec, "ssl_handshake", &callback);
        }

        // Turn off the timeout on the tcp_stream, because
        // the websocket stream has its own timeout system.
        beast::get_lowest_layer(ws_).expires_never();

        // Set suggested timeout settings for the websocket
        ws_.set_option(
            websocket::stream_base::timeout::suggested(
                beast::role_type::client));

        // Set a decorator to change the User-Agent of the handshake
        ws_.set_option(websocket::stream_base::decorator(
            [](websocket::request_type& req)
            {
                req.set(http::field::user_agent,
                    std::string(BOOST_BEAST_VERSION_STRING) +
                        " websocket-client-coro");
            }));

        // Perform the websocket handshake
        ws_.async_handshake(host_, path_, yield[ec]);
        if (ec) {
            return fail(ec, "handshake", &callback);
        }

        SPDLOG_INFO("Websocket {} connected, id={}", url_, id_);
        status_.store(Status::CONNECTED, std::memory_order_release);
    
        // start read messages
        ws_.async_read(
            r_buffer_,
            beast::bind_front_handler(
                &Websocket::on_read,
                shared_from_this()));

        callback(true);
    }

    void close()
    {
        if (status_.load(std::memory_order_relaxed) < Status::DISCONNECTING)
        {
            SPDLOG_INFO("Closing {}:{}...", host_, port_);
            status_.store(Status::DISCONNECTING, std::memory_order_release);
            // Close the WebSocket connection
            ws_.async_close(
                websocket::close_code::normal,
                beast::bind_front_handler(
                    &Websocket::on_close,
                    shared_from_this()));
        }
    }

    void send(const char* buffer, size_t len)
    {
        SPDLOG_DEBUG("--> {}", std::string(buffer, len));
        auto n = asio::buffer_copy(w_buffer_.prepare(len), asio::buffer(buffer, len));
        w_buffer_.commit(n);
        ws_.async_write(
            w_buffer_.data(),
            beast::bind_front_handler(
                &Websocket::on_write,
                shared_from_this()));
    }

private:
    void on_write(beast::error_code ec, std::size_t bytes_transferred)
    {
        if(ec)
        {
            return fail(ec, "write", nullptr, true);
        }
        // SPDLOG_TRACE("{}: {}({}) bytes written", id_, bytes_transferred, w_buffer_.size());
        w_buffer_.consume(bytes_transferred);
    }

    void on_read(beast::error_code ec, std::size_t bytes_transferred)
    {
        if(ec)
        {
            return fail(ec, "read", nullptr, true);
        }

        SPDLOG_TRACE("<-- {}", std::string((const char*)r_buffer_.data().data(), bytes_transferred));
        proxy_->onWsData(id_, (const char*)r_buffer_.data().data(), bytes_transferred, 0);
        // SPDLOG_TRACE("{}: {}({}) bytes read", id_, bytes_transferred, r_buffer_.size());
        r_buffer_.consume(bytes_transferred);

        if (status_.load(std::memory_order_relaxed) == Status::CONNECTED) {
            // read next message
            ws_.async_read(
                r_buffer_,
                beast::bind_front_handler(
                    &Websocket::on_read,
                    shared_from_this()));
        }
    }

    void on_close(beast::error_code ec)
    {
        if (ec && ec != beast::websocket::error::closed)
        {
            fail(ec, "close");
        }

        // If we get here then the connection is closed gracefully
        SPDLOG_INFO("Websocket {}:{} closed", host_, port_);
        status_.store(Status::DISCONNECTED, std::memory_order_release);
        proxy_->onWsClosed(id_);
    }

private:
    void fail(beast::error_code ec, char const *what, std::function<void(bool)> *callback = nullptr, bool close_connection = true)
    {
        auto err_msg = ec.message();
        SPDLOG_ERROR("{}: {} {}", what, ec.value(), err_msg);
        proxy_->onWsError(id_, err_msg.c_str(), err_msg.size());
        if (callback)
        {
            (*callback)(false);
        }
        if (close_connection && status_.load(std::memory_order_relaxed) < Status::DISCONNECTING)
        {
            close();
        }
    }
};

}