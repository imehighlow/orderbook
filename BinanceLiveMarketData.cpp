#include "BinanceLiveMarketData.h"

#include <algorithm>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/ssl/host_name_verification.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <cctype>
#include <format>
#include <iostream>
#include <atomic>
#include <memory>
#include <string>

namespace {
namespace asio = boost::asio;
namespace ssl = asio::ssl;
namespace beast = boost::beast;
namespace ws = beast::websocket;
using tcp = asio::ip::tcp;
} // namespace

struct BinanceLiveMarketData::Session : public std::enable_shared_from_this<Session> {
    Session(asio::io_context& io, ssl::context& tls, std::string host, std::string port,
            std::string target, OnText onText)
        : strand_(asio::make_strand(io)),
          resolver_(strand_),
          ws_(strand_, tls),
          host_(std::move(host)),
          port_(std::move(port)),
          target_(std::move(target)),
          onText_(std::move(onText)) {
    }

    void startAsync() {
        asio::post(strand_, [self = shared_from_this()]() { self->start(); });
    }

    void closeAsync() {
        callbacksSuppressed_.store(true);
        asio::post(strand_, [self = shared_from_this()]() { self->close(); });
    }

  private:
    void start() {
        if (callbacksSuppressed_.load()) {
            return;
        }
        resolver_.async_resolve(host_, port_,
                                beast::bind_front_handler(&Session::onResolve, shared_from_this()));
    }

    void close() {
        resolver_.cancel();
        onText_ = nullptr;

        beast::error_code ignored;
        auto& socket = beast::get_lowest_layer(ws_).socket();
        socket.cancel(ignored);

        if (!ws_.is_open()) {
            socket.shutdown(tcp::socket::shutdown_both, ignored);
            socket.close(ignored);
            return;
        }

        ws_.async_close(ws::close_code::normal, [self = shared_from_this()](beast::error_code) {
            beast::error_code ignored;
            auto& socket = beast::get_lowest_layer(self->ws_).socket();
            socket.shutdown(tcp::socket::shutdown_both, ignored);
            socket.close(ignored);
        });
    }

    void onResolve(beast::error_code ec, const tcp::resolver::results_type& results) {
        if (ec) {
            std::cerr << "BinanceLiveMarketData resolve failed: " << ec.message() << '\n';
            return;
        }
        beast::get_lowest_layer(ws_).expires_after(std::chrono::seconds(10));
        beast::get_lowest_layer(ws_).async_connect(
            results, beast::bind_front_handler(&Session::onConnect, shared_from_this()));
    }

    void onConnect(beast::error_code ec, const tcp::resolver::results_type::endpoint_type&) {
        if (ec) {
            std::cerr << "BinanceLiveMarketData connect failed: " << ec.message() << '\n';
            return;
        }
        if (!SSL_set_tlsext_host_name(ws_.next_layer().native_handle(), host_.c_str())) {
            std::cerr << "BinanceLiveMarketData SNI setup failed\n";
            return;
        }

        ws_.next_layer().set_verify_callback(ssl::host_name_verification(host_));
        beast::get_lowest_layer(ws_).expires_after(std::chrono::seconds(10));
        ws_.next_layer().async_handshake(
            ssl::stream_base::client,
            beast::bind_front_handler(&Session::onTlsHandshake, shared_from_this()));
    }

    void onTlsHandshake(beast::error_code ec) {
        if (ec) {
            std::cerr << "BinanceLiveMarketData TLS handshake failed: " << ec.message() << '\n';
            return;
        }
        beast::get_lowest_layer(ws_).expires_never();
        ws_.set_option(ws::stream_base::timeout::suggested(beast::role_type::client));
        ws_.async_handshake(host_, target_,
                            beast::bind_front_handler(&Session::onWsHandshake, shared_from_this()));
    }

    void onWsHandshake(beast::error_code ec) {
        if (ec) {
            std::cerr << "BinanceLiveMarketData WS handshake failed: " << ec.message() << '\n';
            return;
        }
        doRead();
    }

    void doRead() {
        ws_.async_read(buffer_, beast::bind_front_handler(&Session::onRead, shared_from_this()));
    }

    void onRead(beast::error_code ec, std::size_t) {
        if (ec) {
            if (ec == asio::error::operation_aborted || ec == ws::error::closed ||
                ec == beast::errc::not_connected || ec == asio::error::eof) {
                return;
            }
            std::cerr << "BinanceLiveMarketData read failed: " << ec.message() << '\n';
            return;
        }
        if (!callbacksSuppressed_.load() && onText_) {
            try {
                onText_(beast::buffers_to_string(buffer_.data()));
            } catch (const std::exception& e) {
                std::cerr << "BinanceLiveMarketData onText callback failed: " << e.what() << '\n';
            } catch (...) {
                std::cerr << "BinanceLiveMarketData onText callback failed: unknown exception\n";
            }
        }
        buffer_.consume(buffer_.size());
        doRead();
    }

    asio::strand<asio::io_context::executor_type> strand_;
    tcp::resolver resolver_;
    ws::stream<beast::ssl_stream<beast::tcp_stream>> ws_;
    beast::flat_buffer buffer_;
    std::string host_;
    std::string port_;
    std::string target_;
    OnText onText_;
    std::atomic<bool> callbacksSuppressed_{false};
};

BinanceLiveMarketData::~BinanceLiveMarketData() {
    stop();
}

void BinanceLiveMarketData::start(std::string_view symbol, OnText onText) {
    stop();

    try {
        ensureTlsContextConfigured();
    } catch (const boost::system::system_error& e) {
        std::cerr << "BinanceLiveMarketData TLS context setup failed: " << e.code().message()
                  << '\n';
        return;
    }

    auto session = std::make_shared<Session>(ioContext_, sslContext_, host_, port_,
                                             getTarget(symbol), std::move(onText));

    {
        std::lock_guard<std::mutex> lock(mutex_);
        session_ = session;
    }

    session->startAsync();
}

void BinanceLiveMarketData::stop() {
    std::shared_ptr<Session> session;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        session = session_;
        session_.reset();
    }

    if (session) {
        session->closeAsync();
    }
}

void BinanceLiveMarketData::ensureTlsContextConfigured() {
    std::call_once(tlsContextInitOnce_, [this]() {
        sslContext_.set_default_verify_paths();
        sslContext_.set_verify_mode(ssl::verify_peer);
    });
}

std::string BinanceLiveMarketData::getTarget(std::string_view symbol) const {
    std::string loweredSymbol(symbol);
    std::transform(symbol.begin(), symbol.end(), loweredSymbol.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return std::format("/ws/{}@depth@{}", loweredSymbol, updateSpeedMs_);
}
