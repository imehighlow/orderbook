#pragma once
#include <cstdint>
#include <map>
#include <vector>

using Price = uint64_t;
using Qty = uint64_t;
using BidsMap = std::map<Price, Qty, std::greater<>>;
using AsksMap = std::map<Price, Qty, std::greater<>>;

struct SymbolScales {
    uint64_t priceScale = 1;
    uint64_t qtyScale = 1;
};

struct Level {
    Price price = 0;
    Qty qty = 0;
};

struct OrderBookDelta {
    uint64_t firstUpdate = 0;
    uint64_t lastUpdate = 0;
    std::vector<Level> bids;
    std::vector<Level> asks;
};

struct OrderBookSnapshot {
    uint64_t lastUpdate = 0;
    std::vector<Level> bids;
    std::vector<Level> asks;
};
