// Echo-servern från del 3, med TLS.
//
// Tre tillägg: en ssl::context som laddar certifikat och nyckel, en
// ssl::stream<tcp::socket> som slår in socketen, och en handskakning först i
// echo(). Loopen är densamma, bara på strömmen i stället för socketen.
//
// Testa med:  openssl s_client -connect localhost:7000 -CAfile certs/server.crt -quiet
// nc mot porten misslyckas i handskakningen, för nc talar klartext.
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <array>
#include <format>
#include <print>
#include <string>

namespace asio = boost::asio;
namespace ssl = asio::ssl;
using tcp = asio::ip::tcp;
using asio::awaitable;
using asio::co_spawn;
using asio::detached;
using tls_stream = ssl::stream<tcp::socket>;

auto to_string(tcp::endpoint const& ep) -> std::string {
    return std::format("{}:{}", ep.address().to_string(), ep.port());
}

auto echo(tls_stream stream) -> awaitable<void> {
    auto peer = to_string(stream.lowest_layer().remote_endpoint());  // lowest_layer() är socketen
    std::println("ansluten: {}", peer);

    auto buf = std::array<char, 1024>{};
    auto clean = false;  // klienten skickade close_notify
    try {
        co_await stream.async_handshake(ssl::stream_base::server);
        std::println("handskakning klar: {} ({})", peer, SSL_get_version(stream.native_handle()));
        for (;;) {
            auto n = co_await stream.async_read_some(asio::buffer(buf));
            co_await asio::async_write(stream, asio::buffer(buf, n));
        }
    } catch (boost::system::system_error const& e) {
        if (e.code() == asio::error::eof)
            clean = true;
        else if (e.code() != ssl::error::stream_truncated)  // klienten stängde utan close_notify
            std::println("fel mot {}: {}", peer, e.what());
    }
    if (clean) co_await stream.async_shutdown(asio::as_tuple);  // vårt close_notify, felet ointressant
    std::println("nedkopplad: {}", peer);
}

auto listener(tcp::acceptor acceptor, ssl::context& ctx) -> awaitable<void> {
    for (;;) {
        auto socket = co_await acceptor.async_accept();
        co_spawn(acceptor.get_executor(), echo(tls_stream{std::move(socket), ctx}), detached);
    }
}

int main(int argc, char** argv) try {
    auto port = static_cast<unsigned short>(argc > 1 ? std::stoi(argv[1]) : 7000);
    auto certs = std::string{argc > 2 ? argv[2] : "certs"};

    auto ctx = ssl::context{ssl::context::tls_server};
    ctx.use_certificate_chain_file(certs + "/server.crt");
    ctx.use_private_key_file(certs + "/server.key", ssl::context::pem);

    auto ioc = asio::io_context{};
    auto acceptor = tcp::acceptor{ioc, tcp::endpoint{tcp::v4(), port}};
    std::println("echo-server (TLS) lyssnar på port {}", port);

    co_spawn(ioc, listener(std::move(acceptor), ctx), detached);
    ioc.run();
} catch (std::exception const& e) {
    std::println(stderr, "fel: {}", e.what());
    return 1;
}
