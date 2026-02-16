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
    for (const auto& lvl : snapshot.asks) {
        asks_[lvl.price] = lvl.qty;
    }

    for (const auto& lvl : snapshot.bids) {
        bids_[lvl.price] = lvl.qty;
    }
}

void OrderBook::applyDelta(const OrderBookDelta& delta) {
    applySide(asks_, delta.asks);
    applySide(bids_, delta.bids);
}

const BidsMap& OrderBook::getBids() const {
    return bids_;
}

const AsksMap& OrderBook::getAsks() const {
    return asks_;
}
