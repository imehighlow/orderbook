#include "BinanceSnapshotSource.h"

#include "BinanceAPIParser.h"

#include <algorithm>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/ssl/host_name_verification.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <cctype>
#include <format>
#include <iostream>
#include <memory>
#include <mutex>
#include <openssl/err.h>
#include <string_view>

namespace {
namespace asio = boost::asio;
namespace ssl = asio::ssl;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

constexpr std::string_view kHost = "fapi.binance.com";
constexpr std::string_view kPort = "443";

std::string toUpperCopy(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}
} // namespace

struct BinanceSnapshotSource::Request : public std::enable_shared_from_this<Request> {
    Request(asio::io_context& io, ssl::context& sslContext, std::string depthTarget,
            SymbolScales scales, OnSnapshot onSnapshot)
        : strand_(asio::make_strand(io)),
          resolver_(strand_),
          stream_(strand_, sslContext),
          depthTarget_(std::move(depthTarget)),
          scales_(scales),
          onSnapshot_(std::move(onSnapshot)) {
    }

    void startAsync() {
        asio::post(strand_, [self = shared_from_this()]() { self->start(); });
    }

    void cancelAsync() {
        std::lock_guard<std::mutex> lock(mutex_);
        canceled_ = true;
        onSnapshot_ = nullptr;
        asio::post(strand_, [self = shared_from_this()]() { self->cancel(); });
    }

  private:
    void fail(const char* context, beast::error_code ec) {
        std::cerr << "BinanceSnapshotSource " << context << " failed: " << ec.message() << '\n';
        complete(std::nullopt);
    }

    void failHttp(const char* context) {
        std::cerr << "BinanceSnapshotSource " << context << " HTTP "
                  << static_cast<unsigned>(res_.result_int()) << ": " << res_.reason() << '\n';
        complete(std::nullopt);
    }

    void start() {
        resolver_.async_resolve(kHost, kPort,
                                beast::bind_front_handler(&Request::onResolve, shared_from_this()));
    }

    void cancel() {
        beast::error_code ignored;
        auto& socket = beast::get_lowest_layer(stream_).socket();
        resolver_.cancel();
        socket.cancel(ignored);
        socket.shutdown(tcp::socket::shutdown_both, ignored);
        socket.close(ignored);
    }

    void onResolve(beast::error_code ec, const tcp::resolver::results_type& results) {
        if (ec) {
            fail("resolve", ec);
            return;
        }

        beast::get_lowest_layer(stream_).expires_after(std::chrono::seconds(10));
        beast::get_lowest_layer(stream_).async_connect(
            results, beast::bind_front_handler(&Request::onConnect, shared_from_this()));
    }

    void onConnect(beast::error_code ec, const tcp::resolver::results_type::endpoint_type&) {
        if (ec) {
            fail("connect", ec);
            return;
        }

        if (!SSL_set_tlsext_host_name(stream_.native_handle(), kHost.data())) {
            std::cerr << "BinanceSnapshotSource SNI setup failed\n";
            complete(std::nullopt);
            return;
        }

        stream_.set_verify_callback(ssl::host_name_verification(std::string(kHost)));
        stream_.async_handshake(
            ssl::stream_base::client,
            beast::bind_front_handler(&Request::onTlsHandshake, shared_from_this()));
    }

    void onTlsHandshake(beast::error_code ec) {
        if (ec) {
            fail("TLS handshake", ec);
            return;
        }
        sendDepthRequest();
    }

    void sendDepthRequest() {
        req_ = http::request<http::empty_body>{http::verb::get, depthTarget_, 11};
        req_.set(http::field::host, kHost);
        req_.set(http::field::user_agent, "orderbook/1.0");
        req_.keep_alive(false);

        http::async_write(stream_, req_,
                          beast::bind_front_handler(&Request::onWriteDepth, shared_from_this()));
    }

    void onWriteDepth(beast::error_code ec, std::size_t) {
        if (ec) {
            fail("depth write", ec);
            return;
        }

        http::async_read(stream_, buffer_, res_,
                         beast::bind_front_handler(&Request::onReadDepth, shared_from_this()));
    }

    void onReadDepth(beast::error_code ec, std::size_t) {
        if (ec) {
            fail("depth read", ec);
            return;
        }
        if (res_.result() != http::status::ok) {
            failHttp("depth");
            return;
        }

        const BinanceAPIParser parser{scales_};
        const auto snapshot = parser.parseSnapshot(res_.body());
        if (snapshot.lastUpdate == 0) {
            complete(std::nullopt);
            return;
        }

        result_ = std::move(snapshot);
        stream_.async_shutdown(beast::bind_front_handler(&Request::onShutdown, shared_from_this()));
    }

    void onShutdown(beast::error_code ec) {
        if (ec == asio::ssl::error::stream_truncated || ec == asio::error::eof) {
            ec = {};
        }
        if (ec) {
            fail("shutdown", ec);
            return;
        }

        complete(std::move(result_));
    }

    void complete(std::optional<OrderBookSnapshot> snapshot) {
        OnSnapshot callback;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (done_ || canceled_) {
                done_ = true;
                return;
            }
            done_ = true;
            callback = std::move(onSnapshot_);
        }

        if (callback) {
            callback(std::move(snapshot));
        }
    }

    asio::strand<asio::io_context::executor_type> strand_;
    tcp::resolver resolver_;
    beast::ssl_stream<beast::tcp_stream> stream_;
    beast::flat_buffer buffer_;
    http::request<http::empty_body> req_;
    http::response<http::string_body> res_;
    std::string depthTarget_;
    SymbolScales scales_{};
    std::optional<OrderBookSnapshot> result_;
    OnSnapshot onSnapshot_;
    std::mutex mutex_;
    bool done_ = false;
    bool canceled_ = false;
};

BinanceSnapshotSource::~BinanceSnapshotSource() {
    std::shared_ptr<Request> request;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        request = std::move(request_);
    }
    if (request) {
        request->cancelAsync();
    }
}

void BinanceSnapshotSource::getSnapshotAsync(OnSnapshot onSnapshot) {
    try {
        ensureTlsContextConfigured();
    } catch (const boost::system::system_error& e) {
        std::cerr << "BinanceSnapshotSource TLS context setup failed: " << e.code().message()
                  << '\n';
        if (onSnapshot) {
            onSnapshot(std::nullopt);
        }
        return;
    }

    auto request = std::make_shared<Request>(ioContext_, sslContext_, buildDepthUrl(), scales_,
                                             std::move(onSnapshot));

    std::shared_ptr<Request> oldRequest;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        oldRequest = std::move(request_);
        request_ = request;
    }

    if (oldRequest) {
        oldRequest->cancelAsync();
    }

    request->startAsync();
}

std::string BinanceSnapshotSource::buildDepthUrl() const {
    const std::string upperedSymbol = toUpperCopy(symbol_);
    return std::format("/fapi/v1/depth?symbol={}&limit=1000", upperedSymbol);
}

void BinanceSnapshotSource::ensureTlsContextConfigured() {
    std::call_once(tlsContextInitOnce_, [this]() {
        sslContext_.set_default_verify_paths();
        sslContext_.set_verify_mode(ssl::verify_peer);
    });
}
