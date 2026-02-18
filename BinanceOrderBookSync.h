#pragma once

#include "BinanceAPIParser.h"
#include "ILiveMarketData.h"
#include "IOrderBookSync.h"
#include "ISnapshotSource.h"
#include "OrderBook.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

class BinanceOrderBookSync : public IOrderBookSync {
  public:
    struct SyncStats {
        uint64_t wsMessages = 0;
        uint64_t acceptedDeltas = 0;
        uint64_t droppedDeltas = 0;
        uint64_t resyncs = 0;
        uint64_t snapshotRetries = 0;
    };

    using OnBookUpdated = std::function<void(const OrderBook&, const SymbolScales&, const SyncStats&)>;

    BinanceOrderBookSync(boost::asio::io_context& ioContext, ISnapshotSource& snapshotSource,
                         ILiveMarketData& liveMarketData, SymbolScales scales)
        : strand_(boost::asio::make_strand(ioContext)),
          snapshotSource_(snapshotSource),
          liveMarketData_(liveMarketData),
          scales_(scales),
          parser_(scales) {
    }
    BinanceOrderBookSync() = delete;
    BinanceOrderBookSync(const BinanceOrderBookSync&) = delete;
    BinanceOrderBookSync(BinanceOrderBookSync&&) = delete;
    BinanceOrderBookSync& operator=(const BinanceOrderBookSync&) = delete;
    BinanceOrderBookSync& operator=(BinanceOrderBookSync&&) = delete;
    ~BinanceOrderBookSync() override = default;

  public:
    void onDelta(const OrderBookDelta& delta) override final;
    void onSnapshot(const OrderBookSnapshot& snapshot) override final;
    void start(std::string_view symbol) override final;
    void stop() override final;

    void setOnBookUpdated(OnBookUpdated onBookUpdated);
    const OrderBook& orderBook() const;

  private:
    struct BufferedEvent {
        std::string raw;
        uint64_t firstUpdate = 0;
        uint64_t lastUpdate = 0;
        std::optional<uint64_t> previousLastUpdate;
    };

    enum class State {
        Stopped,
        Bootstrapping,
        Live,
    };

    void startImpl(std::string symbol);
    void stopImpl();
    void restartBootstrap();
    void resetBootstrapBuffer();
    void beginBootstrapCycle();
    void startLiveFeed(uint64_t generation, std::string symbol);
    void onRawText(uint64_t generation, std::string msg);
    void requestSnapshot(uint64_t generation);
    void onSnapshotReady(uint64_t generation, std::optional<OrderBookSnapshot> snapshot);

    bool applyDeltaChecked(const OrderBookDelta& delta, const BufferedEvent& meta);
    void applySnapshotImpl(const OrderBookSnapshot& snapshot);
    void notifyBookUpdated();
    static std::optional<BufferedEvent> parseBufferedEvent(std::string raw);

    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    OrderBook book_{};
    ISnapshotSource& snapshotSource_;
    ILiveMarketData& liveMarketData_;
    OnBookUpdated onBookUpdated_;
    SyncStats stats_{};

    State state_ = State::Stopped;
    std::string symbol_;
    uint64_t generation_ = 0;
    bool snapshotInFlight_ = false;
    std::deque<BufferedEvent> bufferedEvents_;
    bool hasFirstBufferedEvent_ = false;
    uint64_t firstBufferedUpdateId_ = 0;

    SymbolScales scales_{};
    BinanceAPIParser parser_;
};
