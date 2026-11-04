// Daytime-klient över UDP: skicka ett tomt datagram, vänta på svaret.
//
// UDP lovar ingen leverans, så ett svar som inte kommer är normalt flöde, inte
// ett fel. Därför väntar klienten med || mot en timer (mönstret från del 3) och
// försöker om. Ingen connect, ingen ström: ett datagram ut, ett datagram in.
#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <array>
#include <chrono>
#include <format>
#include <print>
#include <string>
#include <string_view>

namespace asio = boost::asio;
using udp = asio::ip::udp;
using asio::awaitable;
using asio::co_spawn;
using namespace asio::experimental::awaitable_operators;
using namespace std::chrono_literals;

constexpr auto timeout = 2s;
constexpr auto attempts = 3;
auto constexpr TIMEOUT = 1;  // index i variant:en från ||

auto to_string(udp::endpoint const& ep) -> std::string {
    return std::format("{}:{}", ep.address().to_string(), ep.port());
}

auto daytime(std::string host, std::string port) -> awaitable<void> {
    auto ex = co_await asio::this_coro::executor;
    auto resolver = udp::resolver{ex};
    auto server = *(co_await resolver.async_resolve(udp::v4(), host, port)).begin();
    auto socket = udp::socket{ex, udp::v4()};  // open, ingen bind: kärnan väljer port
    auto timer = asio::steady_timer{ex};
    auto buf = std::array<char, 64>{};
    auto sender = udp::endpoint{};

    for (auto attempt = 1; attempt <= attempts; ++attempt) {
        co_await socket.async_send_to(asio::buffer("", 0), server);  // tomt datagram räcker

        timer.expires_after(timeout);
        auto result = co_await (socket.async_receive_from(asio::buffer(buf), sender, asio::use_awaitable)
                                || timer.async_wait(asio::use_awaitable));
        if (result.index() == TIMEOUT) {
            std::println("inget svar inom {} (försök {} av {})", timeout, attempt, attempts);
            continue;
        }
        auto n = std::get<0>(result);
        std::print("svar från {}: {}", to_string(sender), std::string_view{buf.data(), n});
        co_return;
    }
    throw std::runtime_error{std::format("{} svarar inte", to_string(server))};
}

int main(int argc, char** argv) try {
    auto host = std::string{argc > 1 ? argv[1] : "localhost"};
    auto port = std::string{argc > 2 ? argv[2] : "1313"};
    auto status = 0;

    auto ioc = asio::io_context{};
    co_spawn(ioc, daytime(host, port), [&status](std::exception_ptr e) {
        if (!e) return;
        try {
            std::rethrow_exception(e);
        } catch (std::exception const& ex) {
            std::println(stderr, "fel: {}", ex.what());
            status = 1;
        }
    });
    ioc.run();
    return status;
} catch (std::exception const& e) {
    std::println(stderr, "fel: {}", e.what());
    return 1;
}
