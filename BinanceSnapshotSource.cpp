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
#include <openssl/err.h>
#include <stdexcept>
#include <string_view>

namespace {
constexpr std::string_view host = "api.binance.com";
constexpr std::string_view port = "443";
} // namespace

OrderBookSnapshot BinanceSnapshotSource::getSnapshot() {
    namespace asio = boost::asio;
    namespace ssl = asio::ssl;
    namespace beast = boost::beast;
    namespace http = beast::http;

    try {
        asio::io_context io;
        ssl::context tls{ssl::context::tls_client};
        tls.set_default_verify_paths();
        tls.set_verify_mode(ssl::verify_peer);

        asio::ip::tcp::resolver resolver{io};
        beast::ssl_stream<beast::tcp_stream> stream{io, tls};

        auto results = resolver.resolve(host, port);
        beast::get_lowest_layer(stream).connect(results);

        if (!SSL_set_tlsext_host_name(stream.native_handle(), host.data())) {
            beast::error_code ec{static_cast<int>(ERR_get_error()),
                                 asio::error::get_ssl_category()};
            throw beast::system_error{ec};
        }

        stream.set_verify_callback(ssl::host_name_verification(std::string(host)));
        stream.handshake(ssl::stream_base::client);

        http::request<http::empty_body> req{http::verb::get, buildUrl(), 11};
        req.set(http::field::host, host);
        req.set(http::field::user_agent, "orderbook/1.0");

        http::write(stream, req);

        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(stream, buffer, res);

        if (res.result() != http::status::ok) {
            throw std::runtime_error(std::format("Binance snapshot HTTP {}: {}",
                                                 static_cast<unsigned>(res.result_int()),
                                                 std::string(res.reason())));
        }

        const std::string responseBody = res.body();

        const BinanceAPIParser parser{symbolScales()};
        const OrderBookSnapshot snapshot = parser.parseSnapshot(responseBody);

        beast::error_code ec;
        auto shutdownEc = stream.shutdown(ec);
        if (shutdownEc == asio::ssl::error::stream_truncated || shutdownEc == asio::error::eof) {
            shutdownEc = {};
        }
        if (shutdownEc) {
            throw beast::system_error{shutdownEc};
        }
        return snapshot;
    } catch (const boost::system::system_error& e) {
        std::cerr << "BinanceSnapshotSource network/TLS error: " << e.what()
                  << " (code=" << e.code().value() << ")\n";
        return {};
    } catch (const std::runtime_error& e) {
        std::cerr << "BinanceSnapshotSource runtime error: " << e.what() << '\n';
        return {};
    } catch (const std::exception& e) {
        std::cerr << "BinanceSnapshotSource unexpected std::exception: " << e.what() << '\n';
        return {};
    } catch (...) {
        std::cerr << "BinanceSnapshotSource unknown failure\n";
        return {};
    }
}

std::string BinanceSnapshotSource::buildUrl() const {
    std::string upperedSymbol(symbol_);
    std::transform(symbol_.begin(), symbol_.end(), upperedSymbol.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return std::format("/api/v3/depth?symbol={}&limit=1000", upperedSymbol);
}

const SymbolScales& BinanceSnapshotSource::symbolScales() {
    if (!symbolScales_.has_value()) {
        symbolScales_ = BinanceScalesSource{}.getScales(symbol_);
    }
    return *symbolScales_;
}
