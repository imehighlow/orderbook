#pragma once

#include "Types.h"

#include <string_view>

class ISnapshotSource {
  public:
    virtual ~ISnapshotSource() = default;
    virtual OrderBookSnapshot getSnapshot(std::string_view symbol) = 0;
};
