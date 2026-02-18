#include "OrderBook.h"

#include "Types.h"

namespace {
template <typename mapT> void applySide(mapT& side, const std::vector<Level>& levels) {
    for (const auto& lvl : levels) {
        if (lvl.qty == 0) {
            side.erase(lvl.price);
            continue;
        }
        side[lvl.price] = lvl.qty;
    }
}
} // namespace

void OrderBook::applySnapshot(const OrderBookSnapshot& snapshot) {
    lastUpdate_ = snapshot.lastUpdate;
    asks_.clear();
    bids_.clear();
    applySide(asks_, snapshot.asks);
    applySide(bids_, snapshot.bids);
}

void OrderBook::applyDelta(const OrderBookDelta& delta) {
    applySide(asks_, delta.asks);
    applySide(bids_, delta.bids);
    lastUpdate_ = delta.lastUpdate;
}

const BidsMap& OrderBook::getBids() const {
    return bids_;
}

const AsksMap& OrderBook::getAsks() const {
    return asks_;
}

uint64_t OrderBook::getLastUpdate() const {
    return lastUpdate_;
}
