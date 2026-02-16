#pragma once

#include "ILiveMarketData.h"
#include "ITextSink.h"
#include "Types.h"

#include <cstdint>

class BinanceLiveMarketData : public ILiveMarketData {
  public:
    void subscribe(std::string_view symbol, ITextSink& sink) override final;

  public:
    // 100 and 1000 are only values allowed by binance, default to 100
    explicit BinanceLiveMarketData(uint64_t updateSpeedMs = 100)
        : updateSpeedMs_(updateSpeedMs == 1000 ? "1000ms" : "100ms") {
    }
    BinanceLiveMarketData(const BinanceLiveMarketData&) = delete;
    BinanceLiveMarketData(BinanceLiveMarketData&&) = delete;
    BinanceLiveMarketData operator=(const BinanceLiveMarketData&) = delete;
    BinanceLiveMarketData operator=(BinanceLiveMarketData&&) = delete;
    virtual ~BinanceLiveMarketData() = default;

  private:
    std::string getTarget(std::string_view symbol) const;
    const std::string host_ = "fstream.binance.com";
    const std::string port_ = "443";
    const std::string updateSpeedMs_;
};
