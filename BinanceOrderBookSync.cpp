#include "BinanceOrderBookSync.h"

void BinanceOrderBookSync::onDelta(const OrderBookDelta& delta) {
    book_.applyDelta(delta);
}

void BinanceOrderBookSync::onSnapshot(const OrderBookSnapshot& snapshot) {
    book_.applySnapshot(snapshot);
}

void BinanceOrderBookSync::start(std::string_view symbol) {
    liveMarketData_.subscribe(symbol, sink_);
    onSnapshot(snapshotSource_.getSnapshot());
}

const OrderBook& BinanceOrderBookSync::orderBook() const {
    return book_;
}
