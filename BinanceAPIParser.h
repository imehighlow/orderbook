#pragma once

#include "Types.h"

#include <string_view>

class BinanceAPIParser {
  public:
    explicit BinanceAPIParser(SymbolScales scales)
        : scales_(scales) {
    }
    BinanceAPIParser(const BinanceAPIParser&) = default;
    BinanceAPIParser(BinanceAPIParser&&) = default;
    BinanceAPIParser& operator=(const BinanceAPIParser&) = delete;
    BinanceAPIParser& operator=(BinanceAPIParser&&) = delete;
    ~BinanceAPIParser() = default;

    OrderBookDelta parseDelta(std::string_view payload) const;
    OrderBookSnapshot parseSnapshot(std::string_view payload) const;

  private:
    SymbolScales scales_{};
};
