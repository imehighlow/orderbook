#pragma once

#include <string_view>

class ITextSink {
  public:
    virtual ~ITextSink() = default;
    virtual void onText(std::string_view) = 0;
};
