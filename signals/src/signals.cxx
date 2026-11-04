// Ctrl-C som en asynkron operation.
//
// signal_set gör en signal till något man kan co_await:a, med samma token och
// samma run() som allt annat. Hanteraren är vanlig kod inne i run(), inte en
// signalhanterare med alla dess begränsningar.
//
// Mönstret graceful shutdown: sluta ta emot nytt, avsluta det som pågår, och
// låt run() returnera av sig själv när inget arbete finns kvar. Inte ioc.stop().
#include <boost/asio.hpp>
#include <chrono>
#include <csignal>
#include <print>
#include <unistd.h>

namespace asio = boost::asio;
using namespace std::chrono_literals;
using asio::awaitable;
using asio::co_spawn;
using asio::detached;

// "Arbetet": en loop som tickar en gång i sekunden tills timern avbryts.
auto ticker(asio::steady_timer& timer) -> awaitable<void> {
    try {
        for (auto n = 1;; ++n) {
            timer.expires_after(1s);
            co_await timer.async_wait();
            std::println("tick {}", n);
        }
    } catch (boost::system::system_error const& e) {
        if (e.code() != asio::error::operation_aborted) throw;
    }

    // Pågående arbete får avslutas, det är det som gör nedstängningen snygg.
    std::println("ticker: avslutar pågående arbete (2 s)");
    timer.expires_after(2s);
    co_await timer.async_wait();
    std::println("ticker: klar");
}

auto shutdown_on_signal(asio::signal_set& signals, asio::steady_timer& timer) -> awaitable<void> {
    auto signo = co_await signals.async_wait();  // returnerar signalnumret
    std::println("fick {}, stänger snyggt (Ctrl-C igen dödar direkt)", signo == SIGINT ? "SIGINT" : "SIGTERM");

    signals.clear();  // lämna tillbaka signalerna: nästa Ctrl-C får default-beteendet
    timer.cancel();   // väcker ticker() med operation_aborted
}

int main() {
    auto ioc = asio::io_context{};
    auto timer = asio::steady_timer{ioc};
    auto signals = asio::signal_set{ioc, SIGINT, SIGTERM};
    std::println("kör som pid {}, avbryt med Ctrl-C eller 'kill {}'", getpid(), getpid());

    co_spawn(ioc, ticker(timer), detached);
    co_spawn(ioc, shutdown_on_signal(signals, timer), detached);

    ioc.run();  // returnerar när båda coroutinerna är klara, inget stop()
    std::println("run() returnerade: nedstängd");
}
