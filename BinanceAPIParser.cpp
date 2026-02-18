#include "BinanceAPIParser.h"

#include <boost/json.hpp>
#include <boost/system/error_code.hpp>
#include <charconv>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>

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

std::optional<uint64_t> parseScaledDecimal(std::string_view s, uint64_t scale, uint32_t places) {
    const size_t dot = s.find('.');
    const std::string_view intPart = (dot == std::string_view::npos) ? s : s.substr(0, dot);
    std::string_view fracPart =
        (dot == std::string_view::npos) ? std::string_view{} : s.substr(dot + 1);

    if (intPart.empty()) {
        return std::nullopt;
    }

    const auto intValue = parseUint(intPart);
    if (!intValue) {
        return std::nullopt;
    }

    if (fracPart.size() > places) {
        // Be tolerant when payload has finer precision than configured scale.
        // Keep the supported precision and drop excess fractional digits.
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

std::optional<uint64_t> parseJsonScaled(const boost::json::value& v, uint64_t scale,
                                        uint32_t places) {
    if (!v.is_string()) {
        return std::nullopt;
    }
    const auto s = v.as_string();
    return parseScaledDecimal(std::string_view(s.data(), s.size()), scale, places);
}

std::optional<std::vector<Level>> parseJsonSide(const boost::json::value& sideValue,
                                                uint64_t priceScale, uint64_t qtyScale,
                                                uint32_t pricePlaces, uint32_t qtyPlaces) {
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
        const auto price = parseJsonScaled(row[0], priceScale, pricePlaces);
        const auto qty = parseJsonScaled(row[1], qtyScale, qtyPlaces);
        if (!price || !qty) {
            return std::nullopt;
        }
        out.push_back(Level{.price = *price, .qty = *qty});
    }
    return out;
}

const boost::json::value* firstExisting(const boost::json::object& obj,
                                        std::initializer_list<std::string_view> keys) {
    for (const auto key : keys) {
        if (const auto* value = obj.if_contains(key)) {
            return value;
        }
    }
    return nullptr;
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
    const auto* firstUpdate = firstExisting(obj, {"U", "firstUpdateId"});
    const auto* lastUpdate = firstExisting(obj, {"u", "finalUpdateId"});
    const auto* bids = firstExisting(obj, {"b", "bids"});
    const auto* asks = firstExisting(obj, {"a", "asks"});

    if (!firstUpdate || !lastUpdate || !bids || !asks) {
        return {};
    }

    const auto pricePlaces = decimalPlacesFromScale(scales_.priceScale);
    const auto qtyPlaces = decimalPlacesFromScale(scales_.qtyScale);
    if (!pricePlaces || !qtyPlaces) {
        return {};
    }

    const auto parsedFirstUpdate = parseJsonU64(*firstUpdate);
    const auto parsedLastUpdate = parseJsonU64(*lastUpdate);
    const auto parsedBids =
        parseJsonSide(*bids, scales_.priceScale, scales_.qtyScale, *pricePlaces, *qtyPlaces);
    const auto parsedAsks =
        parseJsonSide(*asks, scales_.priceScale, scales_.qtyScale, *pricePlaces, *qtyPlaces);
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

    const auto pricePlaces = decimalPlacesFromScale(scales_.priceScale);
    const auto qtyPlaces = decimalPlacesFromScale(scales_.qtyScale);
    if (!pricePlaces || !qtyPlaces) {
        return {};
    }

    const auto parsedLastUpdate = parseJsonU64(*lastUpdateId);
    const auto parsedBids =
        parseJsonSide(*bids, scales_.priceScale, scales_.qtyScale, *pricePlaces, *qtyPlaces);
    const auto parsedAsks =
        parseJsonSide(*asks, scales_.priceScale, scales_.qtyScale, *pricePlaces, *qtyPlaces);
    if (!parsedLastUpdate || !parsedBids || !parsedAsks) {
        return {};
    }
    return OrderBookSnapshot{
        .lastUpdate = *parsedLastUpdate,
        .bids = std::move(*parsedBids),
        .asks = std::move(*parsedAsks),
    };
}

std::string BinanceAPIParser::formatPrice(Price price) const {
    return formatScaled(price, scales_.priceScale);
}

std::string BinanceAPIParser::formatQty(Qty qty) const {
    return formatScaled(qty, scales_.qtyScale);
}

std::string BinanceAPIParser::formatScaled(uint64_t value, uint64_t scale) {
    const auto places = decimalPlacesFromScale(scale);
    if (!places || *places == 0) {
        return std::to_string(value);
    }

    const uint64_t whole = value / scale;
    const uint64_t frac = value % scale;

    std::string fracStr = std::to_string(frac);
    if (fracStr.size() < *places) {
        fracStr.insert(fracStr.begin(), *places - fracStr.size(), '0');
    }
    while (!fracStr.empty() && fracStr.back() == '0') {
        fracStr.pop_back();
    }
    if (fracStr.empty()) {
        return std::to_string(whole) + ".0";
    }

    return std::to_string(whole) + "." + fracStr;
}
