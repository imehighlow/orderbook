#pragma once

#include "Types.h"

#include <string_view>

class IOrderBookSync {
  public:
    virtual ~IOrderBookSync() = default;
    virtual void onDelta(const OrderBookDelta&) = 0;
    virtual void onSnapshot(const OrderBookSnapshot&) = 0;
    virtual void start(std::string_view) = 0;
    virtual void stop() = 0;
};
