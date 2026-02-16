#include "BinanceLiveMarketData.h"

#include <algorithm>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/ssl/host_name_verification.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <cctype>
#include <format>
#include <iostream>

void BinanceLiveMarketData::subscribe(std::string_view symbol, ITextSink& sink) {
    namespace asio = boost::asio;
    namespace ssl = asio::ssl;
    namespace beast = boost::beast;
    namespace ws = beast::websocket;

    using tcp = asio::ip::tcp;
    try {
        asio::io_context io;
        ssl::context tls{ssl::context::tls_client};

        tls.set_default_verify_paths();
        tls.set_verify_mode(ssl::verify_peer);

        tcp::resolver resolver{io};
        ws::stream<beast::ssl_stream<beast::tcp_stream>> sock{io, tls};

        auto results = resolver.resolve(host_, port_);
        beast::get_lowest_layer(sock).expires_after(std::chrono::seconds(10));
        beast::get_lowest_layer(sock).connect(results);

        if (!SSL_set_tlsext_host_name(sock.next_layer().native_handle(), host_.c_str())) {
            beast::error_code ec{static_cast<int>(ERR_get_error()),
                                 asio::error::get_ssl_category()};
            throw beast::system_error{ec};
        }
        sock.next_layer().set_verify_callback(ssl::host_name_verification(host_));

        beast::get_lowest_layer(sock).expires_after(std::chrono::seconds(10));
        sock.next_layer().handshake(ssl::stream_base::client);

        beast::get_lowest_layer(sock).expires_never();
        sock.set_option(ws::stream_base::timeout::suggested(beast::role_type::client));
        sock.handshake(host_, getTarget(symbol));

        beast::flat_buffer buffer;
        while (true) {
            beast::get_lowest_layer(sock).expires_after(std::chrono::seconds(30));
            sock.read(buffer);
            sink.onText(beast::buffers_to_string(buffer.data()));
            buffer.consume(buffer.size());
        }

    } catch (std::exception const& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
    }
}

std::string BinanceLiveMarketData::getTarget(std::string_view symbol) const {
    std::string loweredSymbol(symbol);
    std::transform(symbol.begin(), symbol.end(), loweredSymbol.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return std::format("/ws/{}@depth@{}", loweredSymbol, updateSpeedMs_);
}
