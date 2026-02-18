#include "BinanceOrderBookSync.h"

#include <boost/asio/post.hpp>
#include <boost/json.hpp>
#include <charconv>
#include <iterator>
#include <limits>
#include <optional>
#include <utility>

namespace {
std::optional<uint64_t> parseU64(const boost::json::value& value) {
    if (value.is_uint64()) {
        return value.as_uint64();
    }
    if (value.is_int64()) {
        const auto n = value.as_int64();
        if (n < 0) {
            return std::nullopt;
        }
        return static_cast<uint64_t>(n);
    }
    if (!value.is_string()) {
        return std::nullopt;
    }

    uint64_t out = 0;
    const auto s = value.as_string();
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    if (ec != std::errc() || ptr != s.data() + s.size()) {
        return std::nullopt;
    }
    return out;
}

uint64_t nextUpdateId(uint64_t localUpdate) {
    return (localUpdate == std::numeric_limits<uint64_t>::max()) ? std::numeric_limits<uint64_t>::max()
                                                                  : localUpdate + 1;
}

bool bridgesExpected(const OrderBookDelta& delta, uint64_t expectedUpdate) {
    return delta.firstUpdate <= expectedUpdate && expectedUpdate <= delta.lastUpdate;
}
} // namespace

void BinanceOrderBookSync::onDelta(const OrderBookDelta& delta) {
    boost::asio::post(strand_, [this, delta]() {
        const BufferedEvent meta{
            .raw = {},
            .firstUpdate = delta.firstUpdate,
            .lastUpdate = delta.lastUpdate,
            .previousLastUpdate = std::nullopt,
        };
        (void)applyDeltaChecked(delta, meta);
    });
}

void BinanceOrderBookSync::onSnapshot(const OrderBookSnapshot& snapshot) {
    boost::asio::post(strand_, [this, snapshot]() { applySnapshotImpl(snapshot); });
}

void BinanceOrderBookSync::start(std::string_view symbol) {
    boost::asio::post(strand_, [this, symbol = std::string(symbol)]() { startImpl(symbol); });
}

void BinanceOrderBookSync::stop() {
    boost::asio::post(strand_, [this]() { stopImpl(); });
}

void BinanceOrderBookSync::setOnBookUpdated(OnBookUpdated onBookUpdated) {
    boost::asio::post(strand_,
                      [this, onBookUpdated = std::move(onBookUpdated)]() mutable {
                          onBookUpdated_ = std::move(onBookUpdated);
                      });
}

const OrderBook& BinanceOrderBookSync::orderBook() const {
    return book_;
}

void BinanceOrderBookSync::startImpl(std::string symbol) {
    ++generation_;
    symbol_ = std::move(symbol);
    stats_ = SyncStats{};
    beginBootstrapCycle();
}

void BinanceOrderBookSync::stopImpl() {
    ++generation_;
    state_ = State::Stopped;
    snapshotInFlight_ = false;
    resetBootstrapBuffer();
    symbol_.clear();
    liveMarketData_.stop();
}

void BinanceOrderBookSync::restartBootstrap() {
    if (state_ == State::Stopped || symbol_.empty()) {
        return;
    }

    ++generation_;
    ++stats_.resyncs;
    beginBootstrapCycle();
}

void BinanceOrderBookSync::resetBootstrapBuffer() {
    bufferedEvents_.clear();
    hasFirstBufferedEvent_ = false;
    firstBufferedUpdateId_ = 0;
}

void BinanceOrderBookSync::beginBootstrapCycle() {
    state_ = State::Bootstrapping;
    snapshotInFlight_ = false;
    resetBootstrapBuffer();
    applySnapshotImpl(OrderBookSnapshot{});
    liveMarketData_.stop();
    startLiveFeed(generation_, symbol_);
    requestSnapshot(generation_);
}

void BinanceOrderBookSync::startLiveFeed(uint64_t generation, std::string symbol) {
    liveMarketData_.start(symbol, [this, generation](std::string msg) {
        boost::asio::post(strand_, [this, generation, msg = std::move(msg)]() mutable {
            onRawText(generation, std::move(msg));
        });
    });
}

void BinanceOrderBookSync::onRawText(uint64_t generation, std::string msg) {
    if (generation != generation_ || state_ == State::Stopped) {
        return;
    }

    ++stats_.wsMessages;

    if (state_ == State::Bootstrapping) {
        auto buffered = parseBufferedEvent(std::move(msg));
        if (!buffered) {
            ++stats_.droppedDeltas;
            return;
        }

        if (!hasFirstBufferedEvent_) {
            hasFirstBufferedEvent_ = true;
            firstBufferedUpdateId_ = buffered->firstUpdate;
        }

        bufferedEvents_.push_back(std::move(*buffered));
        if (!snapshotInFlight_) {
            requestSnapshot(generation_);
        }
        return;
    }

    auto meta = parseBufferedEvent(std::move(msg));
    if (!meta) {
        ++stats_.droppedDeltas;
        return;
    }

    const auto delta = parser_.parseDelta(meta->raw);
    (void)applyDeltaChecked(delta, *meta);
}

void BinanceOrderBookSync::requestSnapshot(uint64_t generation) {
    if (snapshotInFlight_ || state_ != State::Bootstrapping) {
        return;
    }

    snapshotInFlight_ = true;
    snapshotSource_.getSnapshotAsync([this, generation](std::optional<OrderBookSnapshot> snapshot) {
        boost::asio::post(strand_, [this, generation, snapshot = std::move(snapshot)]() mutable {
            onSnapshotReady(generation, std::move(snapshot));
        });
    });
}

void BinanceOrderBookSync::onSnapshotReady(uint64_t generation,
                                           std::optional<OrderBookSnapshot> snapshot) {
    if (generation != generation_ || state_ != State::Bootstrapping) {
        return;
    }

    snapshotInFlight_ = false;

    if (!snapshot) {
        ++stats_.snapshotRetries;
        requestSnapshot(generation_);
        return;
    }

    if (hasFirstBufferedEvent_ && snapshot->lastUpdate < firstBufferedUpdateId_) {
        ++stats_.snapshotRetries;
        requestSnapshot(generation_);
        return;
    }

    applySnapshotImpl(*snapshot);

    if (!hasFirstBufferedEvent_) {
        // Stay in bootstrap until at least one WS event is buffered, then
        // snapshot again and validate bridge against that first buffered event.
        return;
    }

    while (!bufferedEvents_.empty() && bufferedEvents_.front().lastUpdate <= book_.getLastUpdate()) {
        ++stats_.droppedDeltas;
        bufferedEvents_.pop_front();
    }

    if (!bufferedEvents_.empty()) {
        const auto& first = bufferedEvents_.front();
        const uint64_t localUpdate = book_.getLastUpdate();
        const uint64_t expectedNext = nextUpdateId(localUpdate);
        if (!(first.firstUpdate <= expectedNext && expectedNext <= first.lastUpdate)) {
            restartBootstrap();
            return;
        }
    }

    if (!bufferedEvents_.empty()) {
        BufferedEvent firstMeta = bufferedEvents_.front();
        // On Binance futures, `pu` of the first event after snapshot may not
        // equal snapshot lastUpdateId; bridge is validated via [U, u].
        firstMeta.previousLastUpdate.reset();
        const auto firstDelta = parser_.parseDelta(firstMeta.raw);
        if (!applyDeltaChecked(firstDelta, firstMeta)) {
            return;
        }
    }

    if (bufferedEvents_.size() > 1) {
        for (auto it = std::next(bufferedEvents_.begin()); it != bufferedEvents_.end(); ++it) {
            const auto delta = parser_.parseDelta(it->raw);
            if (!applyDeltaChecked(delta, *it)) {
                return;
            }
        }
    }

    bufferedEvents_.clear();
    state_ = State::Live;
}

bool BinanceOrderBookSync::applyDeltaChecked(const OrderBookDelta& delta, const BufferedEvent& meta) {
    if (state_ == State::Stopped) {
        return false;
    }

    if (delta.lastUpdate == 0 || delta.firstUpdate == 0) {
        ++stats_.droppedDeltas;
        return true;
    }

    const uint64_t localUpdate = book_.getLastUpdate();
    if (delta.lastUpdate < localUpdate) {
        ++stats_.droppedDeltas;
        return true;
    }

    const uint64_t expectedNext = nextUpdateId(localUpdate);
    const bool hasPrevious = meta.previousLastUpdate.has_value() && *meta.previousLastUpdate != 0;
    const bool sequential =
        hasPrevious ? (*meta.previousLastUpdate == localUpdate || bridgesExpected(delta, expectedNext))
                    : (delta.firstUpdate <= expectedNext);

    if (!sequential) {
        ++stats_.droppedDeltas;
        restartBootstrap();
        return false;
    }

    book_.applyDelta(delta);
    ++stats_.acceptedDeltas;
    notifyBookUpdated();
    return true;
}

void BinanceOrderBookSync::applySnapshotImpl(const OrderBookSnapshot& snapshot) {
    book_.applySnapshot(snapshot);
    notifyBookUpdated();
}

void BinanceOrderBookSync::notifyBookUpdated() {
    if (onBookUpdated_) {
        onBookUpdated_(book_, scales_, stats_);
    }
}

std::optional<BinanceOrderBookSync::BufferedEvent> BinanceOrderBookSync::parseBufferedEvent(
    std::string raw) {
    namespace json = boost::json;

    boost::system::error_code ec;
    const auto parsed = json::parse(raw, ec);
    if (ec || !parsed.is_object()) {
        return std::nullopt;
    }

    const auto& obj = parsed.as_object();
    const auto* firstUpdate = obj.if_contains("U");
    const auto* lastUpdate = obj.if_contains("u");
    if (!firstUpdate || !lastUpdate) {
        return std::nullopt;
    }
    const auto* previousLastUpdate = obj.if_contains("pu");

    const auto first = parseU64(*firstUpdate);
    const auto last = parseU64(*lastUpdate);
    if (!first || !last || *first == 0 || *last == 0 || *first > *last) {
        return std::nullopt;
    }
    std::optional<uint64_t> prev{};
    if (previousLastUpdate) {
        prev = parseU64(*previousLastUpdate);
    }

    return BufferedEvent{
        .raw = std::move(raw),
        .firstUpdate = *first,
        .lastUpdate = *last,
        .previousLastUpdate = prev,
    };
}
