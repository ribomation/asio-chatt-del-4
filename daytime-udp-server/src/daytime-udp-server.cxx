// Daytime-server över UDP (RFC 867): svarar med tiden på varje datagram som kommer.
//
// Skillnaden mot TCP-versionen i del 1: ingen acceptor, ingen anslutning och
// ingen socket per klient. En enda udp::socket tar emot från alla, och
// avsändarens adress följer med varje datagram, så att svaret kan gå tillbaka
// dit. Ett datagram är en hel enhet: inget async_read_until, ingen radbuffert.
#include <boost/asio.hpp>
#include <array>
#include <chrono>
#include <format>
#include <print>
#include <string>

namespace asio = boost::asio;
using udp = asio::ip::udp;
using asio::awaitable;
using asio::co_spawn;
using asio::detached;

auto to_string(udp::endpoint const& ep) -> std::string {
    return std::format("{}:{}", ep.address().to_string(), ep.port());
}

auto now_as_text() -> std::string {
    using namespace std::chrono;
    auto now = zoned_time{current_zone(), floor<seconds>(system_clock::now())};
    return std::format("{:%Y-%m-%d %H:%M:%S %Z}\n", now);
}

auto daytime(udp::socket socket) -> awaitable<void> {
    auto buf = std::array<char, 64>{};  // innehållet spelar ingen roll, bara att något kom
    auto sender = udp::endpoint{};
    for (;;) {
        auto n = co_await socket.async_receive_from(asio::buffer(buf), sender);
        std::println("fråga från {} ({} byte)", to_string(sender), n);
        co_await socket.async_send_to(asio::buffer(now_as_text()), sender);
    }
}

int main(int argc, char** argv) try {
    auto port = static_cast<unsigned short>(argc > 1 ? std::stoi(argv[1]) : 1313);

    auto ioc = asio::io_context{};
    auto socket = udp::socket{ioc, udp::endpoint{udp::v4(), port}};  // bind, inget listen
    std::println("daytime-server (UDP) lyssnar på port {}", port);

    co_spawn(ioc, daytime(std::move(socket)), detached);
    ioc.run();
} catch (std::exception const& e) {
    std::println(stderr, "fel: {}", e.what());
    return 1;
}
