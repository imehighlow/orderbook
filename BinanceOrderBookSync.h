#pragma once

#include "BinanceAPIParser.h"
#include "BinanceTextSink.h"
#include "ILiveMarketData.h"
#include "IOrderBookSync.h"
#include "ISnapshotSource.h"
#include "OrderBook.h"

#include <string_view>

class BinanceOrderBookSync : public IOrderBookSync {
  public:
    BinanceOrderBookSync(ISnapshotSource& snapshotSource, ILiveMarketData& liveMarketData,
                         BinanceAPIParser parser)
        : snapshotSource_(snapshotSource),
          liveMarketData_(liveMarketData),
          sink_([this](const OrderBookDelta& delta) { onDelta(delta); }, std::move(parser)) {
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

    const OrderBook& orderBook() const;

  private:
    OrderBook book_{};
    ISnapshotSource& snapshotSource_;
    ILiveMarketData& liveMarketData_;
    BinanceTextSink sink_;
};
