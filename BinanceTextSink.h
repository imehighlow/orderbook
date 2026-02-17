#pragma once

#include "BinanceAPIParser.h"
#include "ITextSink.h"
#include "Types.h"

#include <functional>
#include <string_view>

class BinanceTextSink : public ITextSink {
  public:
    explicit BinanceTextSink(std::function<void(const OrderBookDelta&)> handler,
                             BinanceAPIParser parser)
        : onDelta_(handler), parser_(std::move(parser)) {
    }
    BinanceTextSink() = delete;
    BinanceTextSink(const BinanceTextSink&) = delete;
    BinanceTextSink(BinanceTextSink&&) = delete;
    BinanceTextSink& operator=(const BinanceTextSink&) = delete;
    BinanceTextSink& operator=(BinanceTextSink&&) = delete;
    ~BinanceTextSink() override = default;

  public:
    void onText(std::string_view msg) override final;
    void setOnDelta(std::function<void(const OrderBookDelta&)> handler);

  private:
    std::function<void(const OrderBookDelta&)> onDelta_;
    const BinanceAPIParser parser_;
};
