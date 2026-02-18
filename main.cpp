#include "BinanceAPIParser.h"
#include "BinanceLiveMarketData.h"
#include "BinanceOrderBookSync.h"
#include "BinanceScalesSource.h"
#include "BinanceSnapshotSource.h"
#include "Renderer.h"
#include "SfmlRenderer.h"

#include <algorithm>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr std::size_t kGuiLevels = 20;
constexpr std::size_t kTerminalLevels = 25;

struct AppOptions {
    std::string symbol = "btcusdt";
    bool useGui = false;
};

struct SharedGuiState {
    std::mutex mutex;
    std::optional<SfmlBookFrame> frame;
};

std::string toUpperCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

template <typename MapT>
void appendFormattedTopLevels(const MapT& side, BinanceAPIParser& formatter,
                              std::vector<std::pair<std::string, std::string>>& out,
                              std::size_t levels) {
    std::size_t count = 0;
    for (const auto& [price, qty] : side) {
        if (count++ >= levels) {
            break;
        }
        out.push_back({formatter.formatPrice(price), formatter.formatQty(qty)});
    }
}

AppOptions parseArgs(int argc, char** argv) {
    AppOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--gui") {
            options.useGui = true;
            continue;
        }
        options.symbol = arg;
    }
    options.symbol = toUpperCopy(options.symbol);
    return options;
}

void setGuiBookCallback(BinanceOrderBookSync& sync, SharedGuiState& shared) {
    sync.setOnBookUpdated([&shared](const OrderBook& book, const SymbolScales& scales,
                                    const BinanceOrderBookSync::SyncStats& stats) {
        BinanceAPIParser formatter(scales);

        SfmlBookFrame next;
        next.lastUpdate = book.getLastUpdate();
        next.stats = stats;
        next.asks.reserve(kGuiLevels);
        next.bids.reserve(kGuiLevels);
        appendFormattedTopLevels(book.getAsks(), formatter, next.asks, kGuiLevels);
        appendFormattedTopLevels(book.getBids(), formatter, next.bids, kGuiLevels);

        std::lock_guard<std::mutex> lock(shared.mutex);
        shared.frame = std::move(next);
    });
}

int runGuiMode(boost::asio::io_context& io, BinanceOrderBookSync& sync, std::string_view symbol) {
    SharedGuiState shared;
    setGuiBookCallback(sync, shared);

    sync.start(std::string(symbol));
    std::thread ioThread([&io]() { io.run(); });

    SfmlRenderer renderer(std::string(symbol), kGuiLevels);
    const bool uiStarted = renderer.run(
        [&shared]() -> std::optional<SfmlBookFrame> {
            std::lock_guard<std::mutex> lock(shared.mutex);
            return shared.frame;
        },
        [&sync, &io]() {
            sync.stop();
            io.stop();
        });

    if (!uiStarted) {
        sync.stop();
        io.stop();
    }
    if (ioThread.joinable()) {
        ioThread.join();
    }

    return uiStarted ? EXIT_SUCCESS : EXIT_FAILURE;
}

void setTerminalBookCallback(BinanceOrderBookSync& sync, std::optional<Renderer>& renderer,
                             const std::string& symbol) {
    sync.setOnBookUpdated([&renderer, &symbol](const OrderBook& book, const SymbolScales& scales,
                                               const BinanceOrderBookSync::SyncStats& stats) {
        if (!renderer) {
            renderer.emplace(symbol, scales, kTerminalLevels);
        }
        renderer->render(book, stats);
    });
}

int runTerminalMode(boost::asio::io_context& io, BinanceOrderBookSync& sync,
                    const std::string& symbol) {
    std::optional<Renderer> renderer;
    setTerminalBookCallback(sync, renderer, symbol);

    boost::asio::signal_set signals(io, SIGINT, SIGTERM);
    signals.async_wait([&](const boost::system::error_code&, int) {
        sync.stop();
        io.stop();
    });

    sync.start(symbol);
    io.run();
    return EXIT_SUCCESS;
}
} // namespace

int main(int argc, char** argv) {
    const AppOptions options = parseArgs(argc, argv);

    try {
        boost::asio::io_context io;

        BinanceScalesSource scalesSource;
        const SymbolScales scales = scalesSource.getScales(options.symbol);
        BinanceLiveMarketData liveMarketData(io);
        BinanceSnapshotSource snapshotSource(io, options.symbol, scales);
        BinanceOrderBookSync sync(io, snapshotSource, liveMarketData, scales);

        return options.useGui ? runGuiMode(io, sync, options.symbol)
                              : runTerminalMode(io, sync, options.symbol);
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
}
