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
#include <limits>
#include <openssl/err.h>
#include <optional>
#include <stdexcept>

namespace {
namespace asio = boost::asio;
namespace ssl = asio::ssl;
namespace beast = boost::beast;
namespace http = beast::http;
namespace json = boost::json;

constexpr std::string_view host = "fapi.binance.com";
constexpr std::string_view port = "443";
constexpr uint64_t kMinPriceScale = 100000000ULL;

std::string toUpperCopy(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}

std::optional<uint64_t> scaleFromPrecisionField(const boost::json::object& symbolObj,
                                                std::string_view fieldName) {
    const auto* value = symbolObj.if_contains(fieldName);
    if (!value) {
        return std::nullopt;
    }

    int64_t precision = -1;
    if (value->is_int64()) {
        precision = value->as_int64();
    } else if (value->is_uint64()) {
        const auto raw = value->as_uint64();
        if (raw > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
            return std::nullopt;
        }
        precision = static_cast<int64_t>(raw);
    } else {
        return std::nullopt;
    }

    if (precision <= 0) {
        return uint64_t{1};
    }

    uint64_t scale = 1;
    for (int64_t i = 0; i < precision; ++i) {
        if (scale > std::numeric_limits<uint64_t>::max() / 10) {
            return std::nullopt;
        }
        scale *= 10;
    }
    return scale;
}

uint64_t scaleFromStepValue(std::string_view step) {
    const auto dotPos = step.find('.');
    if (dotPos == std::string_view::npos) {
        return 1;
    }

    const size_t decimals = step.size() - dotPos - 1;
    uint64_t scale = 1;
    for (size_t i = 0; i < decimals; ++i) {
        scale *= 10;
    }
    return scale;
}

std::string fetchExchangeInfoBody(std::string_view target) {
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

    http::request<http::empty_body> req{http::verb::get, std::string(target), 11};
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

    beast::error_code ec;
    (void)stream.shutdown(ec);

    return std::move(res.body());
}

json::value parseExchangeInfo(std::string_view body) {
    boost::system::error_code parseEc;
    auto parsed = json::parse(body, parseEc);
    if (parseEc || !parsed.is_object()) {
        throw std::runtime_error("exchangeInfo response is not a valid JSON object");
    }
    return parsed;
}

const json::object& findSymbolObject(const json::object& root, std::string_view wantedSymbol) {
    const auto* symbolsValue = root.if_contains("symbols");
    if (!symbolsValue || !symbolsValue->is_array()) {
        throw std::runtime_error("exchangeInfo response missing symbols array");
    }

    const auto& symbols = symbolsValue->as_array();
    if (symbols.empty()) {
        throw std::runtime_error("exchangeInfo.symbols is empty");
    }

    for (const auto& symbolValue : symbols) {
        if (!symbolValue.is_object()) {
            continue;
        }
        const auto& symbolObj = symbolValue.as_object();
        const auto* symbolName = symbolObj.if_contains("symbol");
        if (symbolName && symbolName->is_string() &&
            std::string_view(symbolName->as_string().data(), symbolName->as_string().size()) ==
                wantedSymbol) {
            return symbolObj;
        }
    }

    throw std::runtime_error(std::format("Symbol not found in exchangeInfo: {}", wantedSymbol));
}

std::pair<std::string, std::string> extractTickAndStep(const json::object& symbolObj) {
    const auto* filtersValue = symbolObj.if_contains("filters");
    if (!filtersValue || !filtersValue->is_array()) {
        throw std::runtime_error("exchangeInfo.symbol.filters is missing");
    }

    std::string tickSize;
    std::string stepSize;
    for (const auto& filterValue : filtersValue->as_array()) {
        if (!filterValue.is_object()) {
            continue;
        }
        const auto& filter = filterValue.as_object();
        const auto* filterType = filter.if_contains("filterType");
        if (!filterType || !filterType->is_string()) {
            continue;
        }

        const auto type = std::string_view(filterType->as_string().data(),
                                           filterType->as_string().size());
        if (type == "PRICE_FILTER") {
            const auto* tick = filter.if_contains("tickSize");
            if (tick && tick->is_string()) {
                tickSize = std::string(tick->as_string().data(), tick->as_string().size());
            }
        } else if (type == "LOT_SIZE") {
            const auto* step = filter.if_contains("stepSize");
            if (step && step->is_string()) {
                stepSize = std::string(step->as_string().data(), step->as_string().size());
            }
        }
    }

    if (tickSize.empty() || stepSize.empty()) {
        throw std::runtime_error("Missing PRICE_FILTER.tickSize or LOT_SIZE.stepSize");
    }

    return {std::move(tickSize), std::move(stepSize)};
}

SymbolScales buildScales(const json::object& symbolObj, std::string_view tickSize,
                         std::string_view stepSize) {
    SymbolScales scales{};
    scales.priceScale = scaleFromStepValue(tickSize);
    scales.qtyScale = scaleFromStepValue(stepSize);
    if (const auto precisionScale = scaleFromPrecisionField(symbolObj, "pricePrecision")) {
        scales.priceScale = std::max(scales.priceScale, *precisionScale);
    }
    if (const auto precisionScale = scaleFromPrecisionField(symbolObj, "quantityPrecision")) {
        scales.qtyScale = std::max(scales.qtyScale, *precisionScale);
    }
    scales.priceScale = std::max(scales.priceScale, kMinPriceScale);
    return scales;
}
} // namespace

SymbolScales BinanceScalesSource::getScales(std::string_view symbol) const {
    const std::string body = fetchExchangeInfoBody(buildUrl(symbol));
    const auto parsed = parseExchangeInfo(body);
    const auto& root = parsed.as_object();
    const std::string wantedSymbol = toUpperCopy(symbol);
    const auto& symbolObj = findSymbolObject(root, wantedSymbol);
    const auto [tickSize, stepSize] = extractTickAndStep(symbolObj);
    return buildScales(symbolObj, tickSize, stepSize);
}

std::string BinanceScalesSource::buildUrl(std::string_view symbol) const {
    std::string upperSymbol(symbol);
    std::transform(upperSymbol.begin(), upperSymbol.end(), upperSymbol.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return std::format("/fapi/v1/exchangeInfo?symbol={}", upperSymbol);
}

uint64_t BinanceScalesSource::scaleFromStep(std::string_view step) {
    const auto dotPos = step.find('.');
    if (dotPos == std::string_view::npos) {
        return 1;
    }

    // Preserve full fractional width from exchange metadata. Trimming trailing
    // zeros can under-estimate precision for some symbols.
    const size_t decimals = step.size() - dotPos - 1;
    uint64_t scale = 1;
    for (size_t i = 0; i < decimals; ++i) {
        scale *= 10;
    }
    return scale;
}
