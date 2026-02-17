#pragma once

#include "Types.h"

#include <cstdint>
#include <string>
#include <string_view>

class BinanceScalesSource {
  public:
    BinanceScalesSource() = default;
    BinanceScalesSource(const BinanceScalesSource&) = delete;
    BinanceScalesSource(BinanceScalesSource&&) = delete;
    BinanceScalesSource& operator=(const BinanceScalesSource&) = delete;
    BinanceScalesSource& operator=(BinanceScalesSource&&) = delete;
    ~BinanceScalesSource() = default;

    SymbolScales getScales(std::string_view symbol) const;

  private:
    std::string buildUrl(std::string_view symbol) const;
    static uint64_t scaleFromStep(std::string_view step);
};
