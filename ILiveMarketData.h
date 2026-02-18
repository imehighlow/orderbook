#pragma once

#include <functional>
#include <string>
#include <string_view>

class ILiveMarketData {
  public:
    using OnText = std::function<void(std::string)>;

    virtual ~ILiveMarketData() = default;
    virtual void start(std::string_view symbol, OnText onText) = 0;
    virtual void stop() = 0;
};
