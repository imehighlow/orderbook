#pragma once

#include "ILiveMarketData.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>
#include <cstdint>
#include <memory>
#include <mutex>

class BinanceLiveMarketData : public ILiveMarketData {
  public:
    void start(std::string_view symbol, OnText onText) override final;
    void stop() override final;
    explicit BinanceLiveMarketData(boost::asio::io_context& ioContext, uint64_t updateSpeedMs = 100)
        : ioContext_(ioContext),
          sslContext_(boost::asio::ssl::context::tls_client),
          updateSpeedMs_(updateSpeedMs == 1000 ? "1000ms" : "100ms") {
    }
    ~BinanceLiveMarketData() override;

  private:
    struct Session;
    void ensureTlsContextConfigured();
    std::string getTarget(std::string_view symbol) const;

    const std::string host_ = "fstream.binance.com";
    const std::string port_ = "443";
    boost::asio::io_context& ioContext_;
    boost::asio::ssl::context sslContext_;
    const std::string updateSpeedMs_;
    std::mutex mutex_;
    std::once_flag tlsContextInitOnce_;
    std::shared_ptr<Session> session_;
};
