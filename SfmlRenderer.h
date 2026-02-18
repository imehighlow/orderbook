#pragma once

#include "BinanceOrderBookSync.h"

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

struct SfmlBookFrame {
    uint64_t lastUpdate = 0;
    BinanceOrderBookSync::SyncStats stats{};
    std::vector<std::pair<std::string, std::string>> asks;
    std::vector<std::pair<std::string, std::string>> bids;
};

class SfmlRenderer {
  public:
    explicit SfmlRenderer(std::string symbol, std::size_t levels = 20)
        : symbol_(std::move(symbol)), levelCount_(levels) {
    }

    bool run(const std::function<std::optional<SfmlBookFrame>()>& getFrame,
             const std::function<void()>& onClose);

  private:
    std::string symbol_;
    std::size_t levelCount_;
};
