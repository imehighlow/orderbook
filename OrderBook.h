#pragma once

#include "Types.h"

class OrderBook {
  public:
    void applySnapshot(const OrderBookSnapshot& snapshot);
    void applyDelta(const OrderBookDelta& delta);
    const BidsMap& getBids() const;
    const AsksMap& getAsks() const;
    uint64_t getLastUpdate() const;

  private:
    uint64_t lastUpdate_ = 0;
    AsksMap asks_;
    BidsMap bids_;
};
