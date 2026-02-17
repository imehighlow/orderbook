#include "BinanceAPIParser.h"

#include <boost/json.hpp>
#include <boost/system/error_code.hpp>
#include <charconv>
#include <limits>
#include <optional>
#include <stdexcept>

namespace {
std::optional<uint32_t> decimalPlacesFromScale(uint64_t scale) {
    if (scale == 0) {
        return std::nullopt;
    }
    uint32_t places = 0;
    while (scale > 1) {
        if ((scale % 10) != 0) {
            return std::nullopt;
        }
        scale /= 10;
        ++places;
    }
    return places;
}

std::optional<uint64_t> parseUint(std::string_view s) {
    if (s.empty()) {
        return std::nullopt;
    }
    uint64_t out = 0;
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    if (ec != std::errc() || ptr != s.data() + s.size()) {
        return std::nullopt;
    }
    return out;
}

std::optional<uint64_t> parseScaledDecimal(std::string_view s, uint64_t scale) {
    const auto placesOpt = decimalPlacesFromScale(scale);
    if (!placesOpt) {
        return std::nullopt;
    }
    const uint32_t places = *placesOpt;

    const size_t dot = s.find('.');
    const std::string_view intPart = (dot == std::string_view::npos) ? s : s.substr(0, dot);
    std::string_view fracPart = (dot == std::string_view::npos) ? std::string_view{} : s.substr(dot + 1);

    if (intPart.empty()) {
        return std::nullopt;
    }

    const auto intValue = parseUint(intPart);
    if (!intValue) {
        return std::nullopt;
    }

    if (fracPart.size() > places) {
        for (size_t i = places; i < fracPart.size(); ++i) {
            if (fracPart[i] != '0') {
                return std::nullopt;
            }
        }
        fracPart = fracPart.substr(0, places);
    }

    uint64_t fracValue = 0;
    if (!fracPart.empty()) {
        const auto parsedFrac = parseUint(fracPart);
        if (!parsedFrac) {
            return std::nullopt;
        }
        fracValue = *parsedFrac;
    }

    for (size_t i = fracPart.size(); i < places; ++i) {
        if (fracValue > std::numeric_limits<uint64_t>::max() / 10) {
            return std::nullopt;
        }
        fracValue *= 10;
    }

    if (*intValue > (std::numeric_limits<uint64_t>::max() - fracValue) / scale) {
        return std::nullopt;
    }
    return (*intValue * scale) + fracValue;
}

std::optional<uint64_t> parseJsonU64(const boost::json::value& v) {
    if (v.is_uint64()) {
        return v.as_uint64();
    }
    if (v.is_int64()) {
        const auto n = v.as_int64();
        if (n < 0) {
            return std::nullopt;
        }
        return static_cast<uint64_t>(n);
    }
    if (!v.is_string()) {
        return std::nullopt;
    }
    const auto s = v.as_string();
    return parseUint(std::string_view(s.data(), s.size()));
}

std::optional<uint64_t> parseJsonScaled(const boost::json::value& v, uint64_t scale) {
    if (!v.is_string()) {
        return std::nullopt;
    }
    const auto s = v.as_string();
    return parseScaledDecimal(std::string_view(s.data(), s.size()), scale);
}

std::optional<std::vector<Level>> parseJsonSide(const boost::json::value& sideValue,
                                                uint64_t priceScale,
                                                uint64_t qtyScale) {
    if (!sideValue.is_array()) {
        return std::nullopt;
    }
    const auto& side = sideValue.as_array();
    std::vector<Level> out;
    out.reserve(side.size());
    for (const auto& rowValue : side) {
        if (!rowValue.is_array()) {
            return std::nullopt;
        }
        const auto& row = rowValue.as_array();
        if (row.size() < 2) {
            return std::nullopt;
        }
        const auto price = parseJsonScaled(row[0], priceScale);
        const auto qty = parseJsonScaled(row[1], qtyScale);
        if (!price || !qty) {
            return std::nullopt;
        }
        out.push_back(Level{.price = *price, .qty = *qty});
    }
    return out;
}
} // namespace

OrderBookDelta BinanceAPIParser::parseDelta(std::string_view input) const {
    namespace json = boost::json;
    boost::system::error_code parseError;
    json::value parsed = json::parse(input, parseError);
    if (parseError || !parsed.is_object()) {
        return {};
    }

    const auto& obj = parsed.as_object();
    const auto* firstUpdate = obj.if_contains("U");
    if (!firstUpdate) {
        firstUpdate = obj.if_contains("firstUpdateId");
    }

    const auto* lastUpdate = obj.if_contains("u");
    if (!lastUpdate) {
        lastUpdate = obj.if_contains("finalUpdateId");
    }

    const auto* bids = obj.if_contains("b");
    if (!bids) {
        bids = obj.if_contains("bids");
    }

    const auto* asks = obj.if_contains("a");
    if (!asks) {
        asks = obj.if_contains("asks");
    }

    if (!firstUpdate || !lastUpdate || !bids || !asks) {
        return {};
    }

    const auto parsedFirstUpdate = parseJsonU64(*firstUpdate);
    const auto parsedLastUpdate = parseJsonU64(*lastUpdate);
    const auto parsedBids = parseJsonSide(*bids, scales_.priceScale, scales_.qtyScale);
    const auto parsedAsks = parseJsonSide(*asks, scales_.priceScale, scales_.qtyScale);
    if (!parsedFirstUpdate || !parsedLastUpdate || !parsedBids || !parsedAsks) {
        return {};
    }

    return OrderBookDelta{
        .firstUpdate = *parsedFirstUpdate,
        .lastUpdate = *parsedLastUpdate,
        .bids = std::move(*parsedBids),
        .asks = std::move(*parsedAsks),
    };
}

OrderBookSnapshot BinanceAPIParser::parseSnapshot(std::string_view input) const {
    namespace json = boost::json;
    boost::system::error_code parseError;
    json::value parsed = json::parse(input, parseError);
    if (parseError || !parsed.is_object()) {
        return {};
    }
    const auto& obj = parsed.as_object();
    const auto* lastUpdateId = obj.if_contains("lastUpdateId");
    const auto* bids = obj.if_contains("bids");
    const auto* asks = obj.if_contains("asks");
    if (!lastUpdateId || !bids || !asks) {
        return {};
    }

    const auto parsedLastUpdate = parseJsonU64(*lastUpdateId);
    const auto parsedBids = parseJsonSide(*bids, scales_.priceScale, scales_.qtyScale);
    const auto parsedAsks = parseJsonSide(*asks, scales_.priceScale, scales_.qtyScale);
    if (!parsedLastUpdate || !parsedBids || !parsedAsks) {
        return {};
    }
    return OrderBookSnapshot{
        .lastUpdate = *parsedLastUpdate,
        .bids = std::move(*parsedBids),
        .asks = std::move(*parsedAsks),
    };
}
