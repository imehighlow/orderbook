#include "SfmlRenderer.h"

#include <SFML/Graphics.hpp>
#include <algorithm>
#include <optional>
#include <sstream>
#include <string>

namespace {
std::optional<sf::Font> loadBundledFont() {
    sf::Font font;
    if (!font.openFromFile("assets/fonts/JetBrainsMono-Regular.ttf")) {
        return std::nullopt;
    }
    return font;
}

std::string statsLine(const SfmlBookFrame& frame) {
    std::ostringstream out;
    out << "lastUpdate=" << frame.lastUpdate << "   ws=" << frame.stats.wsMessages
        << "   accepted=" << frame.stats.acceptedDeltas
        << "   dropped=" << frame.stats.droppedDeltas << "   resync=" << frame.stats.resyncs
        << "   snapRetry=" << frame.stats.snapshotRetries;
    return out.str();
}

double parseQty(std::string_view qty) {
    try {
        return std::stod(std::string(qty));
    } catch (...) {
        return 0.0;
    }
}

struct LevelRows {
    std::vector<sf::Text> askLines;
    std::vector<sf::Text> bidLines;
    std::vector<sf::RectangleShape> askBars;
    std::vector<sf::RectangleShape> bidBars;
};

struct Layout {
    float leftColumnX = 24.f;
    float rightColumnX = 620.f;
    float columnWidth = 520.f;
    float rowHeight = 34.f;
    float rowTopY = 132.f;
};

LevelRows makeLevelRows(const sf::Font& font, std::size_t levels) {
    LevelRows rows;
    rows.askLines.reserve(levels);
    rows.bidLines.reserve(levels);
    rows.askBars.reserve(levels);
    rows.bidBars.reserve(levels);

    for (std::size_t i = 0; i < levels; ++i) {
        sf::Text ask(font, "-", 20);
        ask.setFillColor(sf::Color(255, 180, 180));
        rows.askLines.push_back(std::move(ask));

        sf::Text bid(font, "-", 20);
        bid.setFillColor(sf::Color(180, 255, 180));
        rows.bidLines.push_back(std::move(bid));

        sf::RectangleShape askBar;
        askBar.setFillColor(sf::Color(130, 35, 45, 180));
        rows.askBars.push_back(std::move(askBar));

        sf::RectangleShape bidBar;
        bidBar.setFillColor(sf::Color(22, 110, 85, 180));
        rows.bidBars.push_back(std::move(bidBar));
    }
    return rows;
}

void applyLayout(const sf::RenderWindow& window, std::size_t levels, Layout& layout,
                 sf::Text& title, sf::Text& stats, sf::Text& asksHeader, sf::Text& bidsHeader,
                 LevelRows& rows) {
    const sf::Vector2u size = window.getSize();
    const float width = static_cast<float>(size.x);
    const float height = static_cast<float>(size.y);
    const float margin = std::max(14.f, width * 0.02f);
    const float headerTop = std::max(14.f, height * 0.02f);
    const float afterHeaderY = headerTop + 86.f;
    const float bottomMargin = std::max(12.f, height * 0.02f);
    layout.rowHeight =
        std::max(22.f, (height - afterHeaderY - bottomMargin) / static_cast<float>(levels));
    layout.rowTopY = afterHeaderY;
    const float gap = std::max(16.f, width * 0.02f);
    layout.columnWidth = (width - (2.f * margin) - gap);
    layout.columnWidth = std::max(120.f, layout.columnWidth / 2.f);
    layout.leftColumnX = margin;
    layout.rightColumnX = layout.leftColumnX + layout.columnWidth + gap;

    title.setPosition({margin, headerTop});
    stats.setPosition({margin, headerTop + 34.f});
    asksHeader.setPosition({layout.leftColumnX, afterHeaderY - 30.f});
    bidsHeader.setPosition({layout.rightColumnX, afterHeaderY - 30.f});

    const unsigned int textSize =
        static_cast<unsigned int>(std::clamp(layout.rowHeight * 0.52f, 14.f, 22.f));
    asksHeader.setCharacterSize(std::max(16u, textSize + 2));
    bidsHeader.setCharacterSize(std::max(16u, textSize + 2));

    for (std::size_t i = 0; i < levels; ++i) {
        const float y = layout.rowTopY + static_cast<float>(i) * layout.rowHeight;
        rows.askLines[i].setCharacterSize(textSize);
        rows.bidLines[i].setCharacterSize(textSize);
        rows.askLines[i].setPosition({layout.leftColumnX + 8.f, y + 2.f});
        rows.bidLines[i].setPosition({layout.rightColumnX + 8.f, y + 2.f});
        rows.askBars[i].setPosition({layout.leftColumnX, y + 2.f});
        rows.askBars[i].setSize({0.f, std::max(1.f, layout.rowHeight - 4.f)});
        rows.bidBars[i].setPosition({layout.rightColumnX, y + 2.f});
        rows.bidBars[i].setSize({0.f, std::max(1.f, layout.rowHeight - 4.f)});
    }
}

void recreateWindow(sf::RenderWindow& window, bool fullscreen, sf::Vector2u windowedSize,
                    const std::string& windowTitle) {
    if (fullscreen) {
        window.create(sf::VideoMode::getDesktopMode(), windowTitle, sf::Style::Default,
                      sf::State::Fullscreen);
    } else {
        window.create(sf::VideoMode(windowedSize), windowTitle,
                      sf::Style::Titlebar | sf::Style::Resize | sf::Style::Close,
                      sf::State::Windowed);
    }
    window.setVerticalSyncEnabled(true);
    window.setView(sf::View(sf::FloatRect({0.f, 0.f}, {static_cast<float>(window.getSize().x),
                                                       static_cast<float>(window.getSize().y)})));
}

void setWindowView(sf::RenderWindow& window, sf::Vector2u size) {
    window.setView(sf::View(
        sf::FloatRect({0.f, 0.f}, {static_cast<float>(size.x), static_cast<float>(size.y)})));
}

void handleEvents(sf::RenderWindow& window, sf::Vector2u& windowedSize, bool& fullscreen,
                  const std::string& windowTitle, std::size_t levels, Layout& layout,
                  sf::Text& title, sf::Text& stats, sf::Text& asksHeader, sf::Text& bidsHeader,
                  LevelRows& rows) {
    while (const auto event = window.pollEvent()) {
        if (event->is<sf::Event::Closed>()) {
            window.close();
            continue;
        }

        if (const auto* resized = event->getIf<sf::Event::Resized>()) {
            setWindowView(window, resized->size);
            applyLayout(window, levels, layout, title, stats, asksHeader, bidsHeader, rows);
            continue;
        }

        const auto* key = event->getIf<sf::Event::KeyPressed>();
        if (!key) {
            continue;
        }
        if (key->code == sf::Keyboard::Key::Escape) {
            window.close();
            continue;
        }
        if (key->code != sf::Keyboard::Key::F11) {
            continue;
        }

        if (!fullscreen) {
            windowedSize = window.getSize();
        }
        fullscreen = !fullscreen;
        recreateWindow(window, fullscreen, windowedSize, windowTitle);
        applyLayout(window, levels, layout, title, stats, asksHeader, bidsHeader, rows);
    }
}

double maxQty(const std::vector<std::pair<std::string, std::string>>& levels, std::size_t cap) {
    double max = 0.0;
    for (std::size_t i = 0; i < levels.size() && i < cap; ++i) {
        max = std::max(max, parseQty(levels[i].second));
    }
    return max;
}

void updateAskRow(std::size_t i, const SfmlBookFrame& frame, const Layout& layout, LevelRows& rows,
                  double maxAskQty) {
    if (i < frame.asks.size()) {
        rows.askLines[i].setString(frame.asks[i].first + "   " + frame.asks[i].second);
        const double qty = parseQty(frame.asks[i].second);
        const float ratio =
            (maxAskQty > 0.0) ? static_cast<float>(std::clamp(qty / maxAskQty, 0.0, 1.0)) : 0.f;
        const float width = (layout.columnWidth - 4.f) * ratio;
        rows.askBars[i].setSize({width, std::max(1.f, layout.rowHeight - 4.f)});
        rows.askBars[i].setPosition(
            {layout.leftColumnX + layout.columnWidth - width,
             layout.rowTopY + static_cast<float>(i) * layout.rowHeight + 2.f});
    } else {
        rows.askLines[i].setString("-");
        rows.askBars[i].setSize({0.f, std::max(1.f, layout.rowHeight - 4.f)});
    }
}

void updateBidRow(std::size_t i, const SfmlBookFrame& frame, const Layout& layout, LevelRows& rows,
                  double maxBidQty) {
    if (i < frame.bids.size()) {
        rows.bidLines[i].setString(frame.bids[i].first + "   " + frame.bids[i].second);
        const double qty = parseQty(frame.bids[i].second);
        const float ratio =
            (maxBidQty > 0.0) ? static_cast<float>(std::clamp(qty / maxBidQty, 0.0, 1.0)) : 0.f;
        const float width = (layout.columnWidth - 4.f) * ratio;
        rows.bidBars[i].setSize({width, std::max(1.f, layout.rowHeight - 4.f)});
        rows.bidBars[i].setPosition(
            {layout.rightColumnX, layout.rowTopY + static_cast<float>(i) * layout.rowHeight + 2.f});
    } else {
        rows.bidLines[i].setString("-");
        rows.bidBars[i].setSize({0.f, std::max(1.f, layout.rowHeight - 4.f)});
    }
}

void updateVisibleBook(const std::optional<SfmlBookFrame>& latestFrame, const std::string& symbol,
                       const Layout& layout, std::size_t levels, sf::Text& title, sf::Text& stats,
                       LevelRows& rows) {
    title.setString("OrderBook " + symbol);
    if (!latestFrame) {
        stats.setString("Waiting for first synchronized snapshot...");
        for (std::size_t i = 0; i < levels; ++i) {
            rows.askLines[i].setString("-");
            rows.bidLines[i].setString("-");
            rows.askBars[i].setSize({0.f, std::max(1.f, layout.rowHeight - 4.f)});
            rows.bidBars[i].setSize({0.f, std::max(1.f, layout.rowHeight - 4.f)});
        }
        return;
    }

    stats.setString(statsLine(*latestFrame));
    const double maxAskQty = maxQty(latestFrame->asks, levels);
    const double maxBidQty = maxQty(latestFrame->bids, levels);
    for (std::size_t i = 0; i < levels; ++i) {
        updateAskRow(i, *latestFrame, layout, rows, maxAskQty);
        updateBidRow(i, *latestFrame, layout, rows, maxBidQty);
    }
}

void drawFrame(sf::RenderWindow& window, const sf::Text& title, const sf::Text& stats,
               const sf::Text& asksHeader, const sf::Text& bidsHeader, LevelRows& rows) {
    window.clear(sf::Color(18, 20, 24));
    window.draw(title);
    window.draw(stats);
    window.draw(asksHeader);
    window.draw(bidsHeader);
    for (auto& bar : rows.askBars) {
        window.draw(bar);
    }
    for (auto& bar : rows.bidBars) {
        window.draw(bar);
    }
    for (auto& line : rows.askLines) {
        window.draw(line);
    }
    for (auto& line : rows.bidLines) {
        window.draw(line);
    }
    window.display();
}
} // namespace

bool SfmlRenderer::run(const std::function<std::optional<SfmlBookFrame>()>& getFrame,
                       const std::function<void()>& onClose) {
    const std::string windowTitle = "OrderBook - " + symbol_;
    sf::RenderWindow window(sf::VideoMode({1080u, 920u}), windowTitle,
                            sf::Style::Titlebar | sf::Style::Resize | sf::Style::Close);
    window.setVerticalSyncEnabled(true);

    auto fontOpt = loadBundledFont();
    if (!fontOpt) {
        return false;
    }
    sf::Font font = std::move(*fontOpt);

    sf::Text title(font, "", 24);
    title.setFillColor(sf::Color(230, 230, 230));
    title.setPosition({24.f, 16.f});

    sf::Text stats(font, "", 16);
    stats.setFillColor(sf::Color(180, 180, 180));
    stats.setPosition({24.f, 52.f});

    sf::Text asksHeader(font, "Asks", 20);
    asksHeader.setFillColor(sf::Color(255, 120, 120));

    sf::Text bidsHeader(font, "Bids", 20);
    bidsHeader.setFillColor(sf::Color(120, 255, 120));

    LevelRows rows = makeLevelRows(font, levelCount_);

    sf::Vector2u windowedSize = window.getSize();
    bool fullscreen = false;
    Layout layout;
    applyLayout(window, levelCount_, layout, title, stats, asksHeader, bidsHeader, rows);

    std::optional<SfmlBookFrame> latestFrame;

    while (window.isOpen()) {
        handleEvents(window, windowedSize, fullscreen, windowTitle, levelCount_, layout, title, stats,
                     asksHeader, bidsHeader, rows);

        if (auto frame = getFrame()) {
            latestFrame = std::move(frame);
        }

        updateVisibleBook(latestFrame, symbol_, layout, levelCount_, title, stats, rows);
        drawFrame(window, title, stats, asksHeader, bidsHeader, rows);
    }

    if (onClose) {
        onClose();
    }
    return true;
}
