// Asynkron filläsning med stream_file, via io_uring.
//
// En coroutine per fil, alla startade med co_spawn, alla lästa samtidigt i en
// enda tråd. I "Asynkron filläsning med C++20-coroutines" krävdes en
// std::thread per läsning, eftersom vanliga filanrop blockerar. Med io_uring
// gör kärnan filläsningen asynkron på riktigt, och Asio behöver ingen trådpool.
//
// Kräver BOOST_ASIO_HAS_IO_URING och liburing (se CMakeLists.txt). Dessutom
// BOOST_ASIO_HAS_IO_URING_AS_DEFAULT, som en workaround för en bugg i Boost
// 1.92: med epoll som reaktor och io_uring vid sidan om hänger programmet vid
// fler än 128 samtidiga completions, dvs några hundra filer.
#include <boost/asio.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <exception>
#include <filesystem>
#include <print>
#include <ranges>
#include <string>

namespace asio = boost::asio;
namespace fs = std::filesystem;
using asio::awaitable;
using asio::co_spawn;

struct Counts {
    std::size_t lines{};
    std::size_t bytes{};
};

auto count_lines(fs::path path) -> awaitable<Counts> {
    auto ex = co_await asio::this_coro::executor;
    auto file = asio::stream_file{ex, path.string(), asio::stream_file::read_only};
    auto buf = std::array<char, 64 * 1024>{};
    auto lines = 0uz;
    auto bytes = 0uz;
    auto last = '\n';
    try {
        for (;;) {
            auto n = co_await file.async_read_some(asio::buffer(buf));  // samma anrop som för en socket
            lines += std::ranges::count(buf | std::views::take(n), '\n');
            bytes += n;
            last = buf[n - 1];
        }
    } catch (boost::system::system_error const& e) {
        if (e.code() != asio::error::eof) throw;
        if (last != '\n') ++lines;  // sista raden saknade radslut
    }
    std::println("{:>9} rader {:>12} byte  {}", lines, bytes, path.string());
    co_return Counts{lines, bytes};
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::println(stderr, "användning: {} fil...", argv[0]);
        return 1;
    }
    auto start = std::chrono::steady_clock::now();
    auto status = 0;
    auto total = Counts{};

    auto ioc = asio::io_context{};
    for (auto i = 1; i < argc; ++i) {
        auto path = fs::path{argv[i]};
        // awaitable<Counts> ger handlern signaturen (exception_ptr, Counts).
        // Ingen mutex runt total: alla handlers körs inne i run(), i en tråd.
        co_spawn(ioc, count_lines(path), [path, &total, &status](std::exception_ptr e, Counts c) {
            if (e) {
                try {
                    std::rethrow_exception(e);
                } catch (std::exception const& ex) {
                    std::println(stderr, "fel: {}: {}", path.string(), ex.what());
                }
                status = 1;
                return;
            }
            total.lines += c.lines;
            total.bytes += c.bytes;
        });
    }
    ioc.run();  // alla filer läses här, i den här tråden

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    std::println("{:>9} rader {:>12} byte  totalt, {} filer på {}", total.lines, total.bytes, argc - 1, ms);
    return status;
}
