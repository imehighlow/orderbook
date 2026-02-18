#pragma once

#include "Types.h"

#include <string>
#include <string_view>

class BinanceAPIParser {
  public:
    explicit BinanceAPIParser(SymbolScales scales)
        : scales_(scales) {
    }

    OrderBookDelta parseDelta(std::string_view payload) const;
    OrderBookSnapshot parseSnapshot(std::string_view payload) const;
    std::string formatPrice(Price price) const;
    std::string formatQty(Qty qty) const;
    static std::string formatScaled(uint64_t value, uint64_t scale);

  private:
    SymbolScales scales_{};
};
