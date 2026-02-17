#pragma once
#include "BinanceScalesSource.h"
#include "ISnapshotSource.h"
#include "Types.h"

#include <optional>
#include <string>

class BinanceSnapshotSource : public ISnapshotSource {
  public:
    // 100 and 1000 are only values allowed by binance, default to 100
    explicit BinanceSnapshotSource(std::string symbol) : symbol_(std::move(symbol)) {}
    BinanceSnapshotSource(const BinanceSnapshotSource&) = delete;
    BinanceSnapshotSource(BinanceSnapshotSource&&) = delete;
    BinanceSnapshotSource& operator=(const BinanceSnapshotSource&) = delete;
    BinanceSnapshotSource& operator=(BinanceSnapshotSource&&) = delete;
    virtual ~BinanceSnapshotSource() = default;

  public:
    OrderBookSnapshot getSnapshot() override final;

  private:
    std::string buildUrl() const;
    const SymbolScales& symbolScales();
    const std::string symbol_;
    std::optional<SymbolScales> symbolScales_;
};
