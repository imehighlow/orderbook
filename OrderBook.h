#pragma once

#include "Types.h"

class OrderBook {
  public:
    explicit OrderBook() = default;
    OrderBook(const OrderBook&) = delete;
    OrderBook(OrderBook&&) = delete;
    OrderBook operator=(const OrderBook&&) = delete;
    OrderBook operator=(OrderBook&&) = delete;
    ~OrderBook() = default;

  public:
    void applySnapshot(const OrderBookSnapshot& snapshot);
    void applyDelta(const OrderBookDelta& delta);
    const BidsMap& getBids() const;
    const AsksMap& getAsks() const;
    
  private:
    uint64_t lastUpdate_ = 0;
    AsksMap asks_;
    BidsMap bids_;
};
