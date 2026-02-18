#pragma once

#include "Types.h"

#include <functional>
#include <optional>
#include <string_view>

class ISnapshotSource {
  public:
    using OnSnapshot = std::function<void(std::optional<OrderBookSnapshot>)>;

    virtual ~ISnapshotSource() = default;
    virtual void getSnapshotAsync(OnSnapshot onSnapshot) = 0;
};
