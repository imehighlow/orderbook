#include "Renderer.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {
struct RenderData {
    std::vector<std::pair<Price, Qty>> bids;
    std::vector<std::pair<Price, Qty>> asks;
    std::string titleLine;
    std::string timeLine;
    std::string depthLine;
    std::string statsLine;
    std::string tableHeader;
    std::string tableSep;
};

uint32_t decimalPlacesFromScale(uint64_t scale) {
    if (scale <= 1) {
        return 0;
    }
    uint32_t places = 0;
    while (scale > 1) {
        if ((scale % 10) != 0) {
            return 2;
        }
        scale /= 10;
        ++places;
    }
    return places;
}

std::string nowString() {
    const auto now = std::chrono::system_clock::now();
    const auto nowTime = std::chrono::system_clock::to_time_t(now);
    const std::tm* nowTmPtr = std::localtime(&nowTime);
    if (!nowTmPtr) {
        return "-";
    }
    const std::tm nowTm = *nowTmPtr;

    std::ostringstream out;
    out << std::put_time(&nowTm, "%Y-%m-%d %H:%M:%S");
    return out.str();
}

std::string formatMidPrice(Price bid, Price ask, uint64_t scale) {
    const uint64_t sum = bid + ask;
    if ((sum % 2) == 0) {
        return BinanceAPIParser::formatScaled(sum / 2, scale);
    }

    // Half-tick mid: show one extra decimal place beyond price scale.
    const uint32_t places = decimalPlacesFromScale(scale);
    std::ostringstream out;
    out << std::fixed << std::setprecision(places + 1)
        << (static_cast<double>(sum) / 2.0) / static_cast<double>(scale);
    return out.str();
}

template <typename MapT>
std::vector<std::pair<Price, Qty>> topLevels(const MapT& side, std::size_t levels) {
    std::vector<std::pair<Price, Qty>> out;
    out.reserve(levels);
    for (const auto& [price, qty] : side) {
        if (out.size() >= levels) {
            break;
        }
        out.push_back({price, qty});
    }
    return out;
}

std::size_t utf8CodepointCount(std::string_view s) {
    std::size_t count = 0;
    for (unsigned char c : s) {
        if ((c & 0xC0U) != 0x80U) {
            ++count;
        }
    }
    return count;
}

std::string repeatUtf8(std::string_view token, std::size_t count) {
    std::string out;
    out.reserve(token.size() * count);
    for (std::size_t i = 0; i < count; ++i) {
        out += token;
    }
    return out;
}

std::string padRightUtf8(std::string s, std::size_t width) {
    const auto len = utf8CodepointCount(s);
    if (len < width) {
        s.append(width - len, ' ');
    }
    return s;
}

std::string buildTableHeader() {
    std::ostringstream out;
    out << std::setw(15) << "BID QTY"
        << "│" << std::setw(12) << "BID PRICE"
        << "│" << std::setw(12) << "ASK PRICE"
        << "│" << std::setw(15) << "ASK QTY";
    return out.str();
}

std::string buildStatsLine(const OrderBook& book, const BinanceOrderBookSync::SyncStats& stats) {
    std::ostringstream out;
    out << "LastUpdateId=" << book.getLastUpdate()
        << "  Levels=" << (book.getBids().size() + book.getAsks().size())
        << "  WS=" << stats.wsMessages << "  Accepted=" << stats.acceptedDeltas
        << "  Dropped=" << stats.droppedDeltas << "  Resyncs=" << stats.resyncs
        << "  SnapshotRetries=" << stats.snapshotRetries;
    return out.str();
}

std::string buildBookRow(const BinanceAPIParser& formatter,
                         const std::vector<std::pair<Price, Qty>>& bids,
                         const std::vector<std::pair<Price, Qty>>& asks, std::size_t i) {
    std::ostringstream out;
    if (i < bids.size()) {
        out << std::setw(15) << formatter.formatQty(bids[i].second) << "│" << std::setw(12)
            << formatter.formatPrice(bids[i].first) << "│";
    } else {
        out << std::setw(15) << "-" << "│" << std::setw(12) << "-" << "│";
    }
    if (i < asks.size()) {
        out << std::setw(12) << formatter.formatPrice(asks[i].first) << "│" << std::setw(15)
            << formatter.formatQty(asks[i].second);
    } else {
        out << std::setw(12) << "-" << "│" << std::setw(15) << "-";
    }
    return out.str();
}

std::size_t computeContentWidth(const RenderData& data) {
    return std::max({utf8CodepointCount(data.tableHeader), utf8CodepointCount(data.tableSep),
                     utf8CodepointCount(data.titleLine), utf8CodepointCount(data.timeLine),
                     utf8CodepointCount(data.depthLine), utf8CodepointCount(data.statsLine)});
}

RenderData makeRenderData(const OrderBook& book, const BinanceOrderBookSync::SyncStats& stats,
                          std::string_view symbol, std::size_t levels) {
    RenderData data;
    data.bids = topLevels(book.getBids(), levels);
    data.asks = topLevels(book.getAsks(), levels);
    data.titleLine = "LIVE ORDERBOOK  " + std::string(symbol);
    data.timeLine = nowString();
    data.depthLine = "Depth: " + std::to_string(levels);
    data.statsLine = buildStatsLine(book, stats);
    data.tableHeader = buildTableHeader();
    data.tableSep = repeatUtf8("─", 15) + "┼" + repeatUtf8("─", 12) + "┼" + repeatUtf8("─", 12) +
                    "┼" + repeatUtf8("─", 15);
    return data;
}

void printSummary(const BinanceAPIParser& formatter, uint64_t priceScale,
                  const std::vector<std::pair<Price, Qty>>& bids,
                  const std::vector<std::pair<Price, Qty>>& asks,
                  const std::function<void(const std::string&)>& printLine) {
    if (bids.empty() || asks.empty()) {
        return;
    }

    const Price spreadTicks = asks.front().first - bids.front().first;
    const std::string midPrice = formatMidPrice(bids.front().first, asks.front().first, priceScale);
    const double midPriceForBps =
        (static_cast<double>(bids.front().first + asks.front().first) / 2.0) /
        static_cast<double>(priceScale);
    const double spreadForBps = static_cast<double>(asks.front().first - bids.front().first) /
                                static_cast<double>(priceScale);
    const double spreadBps =
        (midPriceForBps == 0.0) ? 0.0 : (spreadForBps / midPriceForBps) * 10000.0;

    std::ostringstream spreadOut;
    spreadOut << std::fixed << std::setprecision(1) << spreadBps;
    printLine("Best Bid : $" + formatter.formatPrice(bids.front().first));
    printLine("Best Ask : $" + formatter.formatPrice(asks.front().first));
    printLine("Spread   : $" + formatter.formatPrice(spreadTicks) + " (" + spreadOut.str() +
              " bps)");
    printLine("Mid Price: $" + midPrice);
}
} // namespace

void Renderer::render(const OrderBook& book, const BinanceOrderBookSync::SyncStats& stats) {
    std::cout << "\x1b[2J\x1b[H";

    const RenderData data = makeRenderData(book, stats, symbol_, levels_);
    const std::size_t contentWidth = computeContentWidth(data);
    const auto printLine = [&contentWidth](const std::string& line) {
        std::cout << padRightUtf8(line, contentWidth) << "\n";
    };

    printLine(data.titleLine);
    printLine(data.timeLine);
    printLine(data.depthLine);
    printLine("");
    printLine(data.tableHeader);
    printLine(data.tableSep);

    const std::size_t rows = std::max(data.bids.size(), data.asks.size());
    for (std::size_t i = 0; i < rows; ++i) {
        printLine(buildBookRow(formatter_, data.bids, data.asks, i));
    }

    printLine("");
    printSummary(formatter_, scales_.priceScale, data.bids, data.asks, printLine);

    printLine("");
    printLine(data.statsLine);

    std::cout.flush();
}
