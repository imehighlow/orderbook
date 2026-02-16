#pragma once

#include "ITextSink.h"

#include <string_view>

class ILiveMarketData {
  public:
    virtual ~ILiveMarketData() = default;
    virtual void subscribe(std::string_view symbol, ITextSink& sink) = 0;
};
