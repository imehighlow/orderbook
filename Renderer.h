#pragma once

#include "BinanceAPIParser.h"
#include "BinanceOrderBookSync.h"
#include "OrderBook.h"
#include "Types.h"

#include <cstddef>
#include <string>

class Renderer {
  public:
    explicit Renderer(std::string symbol, SymbolScales scales, std::size_t levels = 25)
        : scales_(scales), formatter_(scales), symbol_(std::move(symbol)), levels_(levels) {
    }

    void render(const OrderBook& book, const BinanceOrderBookSync::SyncStats& stats);

  private:
    SymbolScales scales_;
    BinanceAPIParser formatter_;
    std::string symbol_;
    std::size_t levels_;
};
