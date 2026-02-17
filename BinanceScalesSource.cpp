#include "BinanceScalesSource.h"

#include <algorithm>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/ssl/host_name_verification.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/json.hpp>
#include <cctype>
#include <format>
#include <openssl/err.h>
#include <stdexcept>

namespace {
constexpr std::string_view host = "api.binance.com";
constexpr std::string_view port = "443";
} // namespace

SymbolScales BinanceScalesSource::getScales(std::string_view symbol) const {
    namespace asio = boost::asio;
    namespace ssl = asio::ssl;
    namespace beast = boost::beast;
    namespace http = beast::http;
    namespace json = boost::json;

    asio::io_context io;
    ssl::context tls{ssl::context::tls_client};
    tls.set_default_verify_paths();
    tls.set_verify_mode(ssl::verify_peer);

    asio::ip::tcp::resolver resolver{io};
    beast::ssl_stream<beast::tcp_stream> stream{io, tls};

    auto results = resolver.resolve(host, port);
    beast::get_lowest_layer(stream).connect(results);

    if (!SSL_set_tlsext_host_name(stream.native_handle(), host.data())) {
        beast::error_code ec{static_cast<int>(ERR_get_error()), asio::error::get_ssl_category()};
        throw beast::system_error{ec};
    }

    stream.set_verify_callback(ssl::host_name_verification(std::string(host)));
    stream.handshake(ssl::stream_base::client);

    http::request<http::empty_body> req{http::verb::get, buildUrl(symbol), 11};
    req.set(http::field::host, host);
    req.set(http::field::user_agent, "orderbook/1.0");
    http::write(stream, req);

    beast::flat_buffer buffer;
    http::response<http::string_body> res;
    http::read(stream, buffer, res);

    if (res.result() != http::status::ok) {
        throw std::runtime_error(std::format("Binance exchangeInfo HTTP {}: {}",
                                             static_cast<unsigned>(res.result_int()),
                                             std::string(res.reason())));
    }

    const auto& root = json::parse(res.body()).as_object();
    const auto& symbols = root.at("symbols").as_array();
    if (symbols.empty()) {
        throw std::runtime_error("exchangeInfo.symbols is empty");
    }
    const auto& symObj = symbols.front().as_object();
    const auto& filters = symObj.at("filters").as_array();

    std::string tickSize;
    std::string stepSize;
    for (const auto& filterValue : filters) {
        const auto& filter = filterValue.as_object();
        const auto type = std::string_view(filter.at("filterType").as_string().data(),
                                           filter.at("filterType").as_string().size());
        if (type == "PRICE_FILTER") {
            const auto& tick = filter.at("tickSize").as_string();
            tickSize = std::string(tick.data(), tick.size());
        } else if (type == "LOT_SIZE") {
            const auto& step = filter.at("stepSize").as_string();
            stepSize = std::string(step.data(), step.size());
        }
    }

    if (tickSize.empty() || stepSize.empty()) {
        throw std::runtime_error("Missing PRICE_FILTER.tickSize or LOT_SIZE.stepSize");
    }

    SymbolScales scales{};
    scales.priceScale = scaleFromStep(tickSize);
    scales.qtyScale = scaleFromStep(stepSize);

    beast::error_code ec;
    auto shutdownEc = stream.shutdown(ec);
    if (shutdownEc == asio::ssl::error::stream_truncated || shutdownEc == asio::error::eof) {
        shutdownEc = {};
    }
    if (shutdownEc) {
        throw beast::system_error{shutdownEc};
    }

    return scales;
}

std::string BinanceScalesSource::buildUrl(std::string_view symbol) const {
    std::string upperSymbol(symbol);
    std::transform(upperSymbol.begin(), upperSymbol.end(), upperSymbol.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return std::format("/api/v3/exchangeInfo?symbol={}", upperSymbol);
}

uint64_t BinanceScalesSource::scaleFromStep(std::string_view step) {
    const auto dotPos = step.find('.');
    if (dotPos == std::string_view::npos) {
        return 1;
    }

    size_t end = step.size();
    while (end > dotPos + 1 && step[end - 1] == '0') {
        --end;
    }

    const size_t decimals = end - dotPos - 1;
    uint64_t scale = 1;
    for (size_t i = 0; i < decimals; ++i) {
        scale *= 10;
    }
    return scale;
}
