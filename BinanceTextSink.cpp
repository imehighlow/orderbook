#include "BinanceTextSink.h"
#include "Types.h"

void BinanceTextSink::onText(std::string_view msg) {
    OrderBookDelta delta = parser_.parseDelta(msg);
    if (onDelta_) {
        onDelta_(delta);
    }
}

void BinanceTextSink::setOnDelta(std::function<void(const OrderBookDelta&)> handler) {
    onDelta_ = std::move(handler);
}
