#pragma once

#include "ISnapshotSource.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>
#include <memory>
#include <mutex>
#include <string>

class BinanceSnapshotSource : public ISnapshotSource {
  public:
    explicit BinanceSnapshotSource(boost::asio::io_context& ioContext, std::string symbol,
                                   SymbolScales scales)
        : ioContext_(ioContext),
          sslContext_(boost::asio::ssl::context::tls_client),
          symbol_(std::move(symbol)),
          scales_(scales) {
    }
    ~BinanceSnapshotSource() override;

    void getSnapshotAsync(OnSnapshot onSnapshot) override final;

  private:
    struct Request;
    std::string buildDepthUrl() const;
    void ensureTlsContextConfigured();

    boost::asio::io_context& ioContext_;
    boost::asio::ssl::context sslContext_;
    const std::string symbol_;
    const SymbolScales scales_;
    std::once_flag tlsContextInitOnce_;
    std::mutex mutex_;
    std::shared_ptr<Request> request_;
};
