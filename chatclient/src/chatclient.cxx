// Chattklienten från del 3, med TLS och discovery.
//
//   discover():         broadcast "CHAT?" till UDP 4712, vänta på "CHAT <port> tls|no-tls"
//                       med || mot en timer. Avsändaradressen är serverns adress.
//   chat():             anslut, handskakning om TLS, och sedan del 3:s två coroutines
//                       stdin_to_socket() || socket_to_stdout(), nu som mallar på strömmen
//
// Utan värd som argument hittar klienten servern själv, och svaret avgör om
// det blir TLS eller inte. Med värd som argument är klartext default, --tls
// slår på TLS. Certifikatet i --ca (default certs/server.crt) är enda CA, och
// klienten verifierar serverns cert mot det men inte värdnamnet: discovery ger
// en IP-adress som inte behöver stå i certet. I produktion gör man tvärtom.
//
// Kör: chatclient [--tls] [--ca FIL] [värd [port]]
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <array>
#include <chrono>
#include <cstdio>
#include <exception>
#include <print>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unistd.h>

namespace asio = boost::asio;
namespace ssl = asio::ssl;
using tcp = asio::ip::tcp;
using udp = asio::ip::udp;
using asio::awaitable;
using namespace asio::experimental::awaitable_operators;
using namespace std::chrono_literals;
using tls_stream = ssl::stream<tcp::socket>;

constexpr auto discovery_port = static_cast<unsigned short>(4712);
constexpr auto discovery_timeout = 3s;
auto constexpr TIMEOUT = 1;  // index i variant:en från ||

struct Options {
    std::string host;  // tom: hitta servern med discovery
    std::string port = "4711";
    bool tls = false;
    std::string ca = "certs/server.crt";
};

// Broadcast når alla på det lokala nätet, inklusive oss själva. Servern svarar
// från sin adress, och den följer med i sender.
auto discover(Options& opts) -> awaitable<void> {
    auto ex = co_await asio::this_coro::executor;
    auto socket = udp::socket{ex, udp::v4()};
    socket.set_option(udp::socket::broadcast(true));
    auto everyone = udp::endpoint{asio::ip::address_v4::broadcast(), discovery_port};
    co_await socket.async_send_to(asio::buffer("CHAT?", 5), everyone);

    auto timer = asio::steady_timer{ex, discovery_timeout};
    auto buf = std::array<char, 64>{};
    auto sender = udp::endpoint{};
    auto result = co_await (socket.async_receive_from(asio::buffer(buf), sender, asio::use_awaitable) ||
                            timer.async_wait(asio::use_awaitable));
    if (result.index() == TIMEOUT) throw std::runtime_error{std::format("ingen chattserver svarade inom {}", discovery_timeout)};

    auto reply = std::istringstream{std::string{buf.data(), std::get<0>(result)}};
    auto tag = std::string{}, mode = std::string{};
    reply >> tag >> opts.port >> mode;
    if (tag != "CHAT") throw std::runtime_error{"oväntat discovery-svar: " + reply.str()};
    opts.host = sender.address().to_string();
    opts.tls = (mode == "tls");
    std::println("hittade servern på {}:{} ({})", opts.host, opts.port, mode);
}

template <typename Stream>
auto stdin_to_socket(asio::posix::stream_descriptor& in, Stream& stream) -> awaitable<void> {
    auto line = std::string{};
    try {
        for (;;) {
            auto n = co_await asio::async_read_until(in, asio::dynamic_buffer(line), '\n');
            co_await asio::async_write(stream, asio::buffer(line, n));
            line.erase(0, n);
        }
    } catch (boost::system::system_error const& e) {
        if (e.code() != asio::error::eof) throw;
        std::println("(stdin stängd, avslutar)");  // Ctrl-D
    }
}

template <typename Stream>
auto socket_to_stdout(Stream& stream) -> awaitable<void> {
    auto buf = std::string{};
    try {
        for (;;) {
            auto n = co_await asio::async_read_until(stream, asio::dynamic_buffer(buf), '\n');
            std::print("{}", std::string_view{buf}.substr(0, n));
            std::fflush(stdout);
            buf.erase(0, n);
        }
    } catch (boost::system::system_error const& e) {
        if (e.code() != asio::error::eof && e.code() != ssl::error::stream_truncated) throw;
        std::println("(servern stängde)");
    }
}

auto chat(Options opts) -> awaitable<void> {
    auto ex = co_await asio::this_coro::executor;
    if (opts.host.empty()) co_await discover(opts);

    auto resolver = tcp::resolver{ex};
    auto socket = tcp::socket{ex};
    auto in = asio::posix::stream_descriptor{ex, ::dup(STDIN_FILENO)};
    co_await asio::async_connect(socket, co_await resolver.async_resolve(opts.host, opts.port));

    if (!opts.tls) {
        std::println("ansluten till {}:{} (klartext), skriv rader, Ctrl-D avslutar", opts.host, opts.port);
        co_await (stdin_to_socket(in, socket) || socket_to_stdout(socket));
        co_return;
    }

    auto ctx = ssl::context{ssl::context::tls_client};
    ctx.load_verify_file(opts.ca);          // serverns självsignerade cert är enda CA
    ctx.set_verify_mode(ssl::verify_peer);  // verifiera certet, men inte värdnamnet
    auto stream = tls_stream{std::move(socket), ctx};
    co_await stream.async_handshake(ssl::stream_base::client);
    std::println("ansluten till {}:{} ({}), skriv rader, Ctrl-D avslutar", opts.host, opts.port,
                 SSL_get_version(stream.native_handle()));

    co_await (stdin_to_socket(in, stream) || socket_to_stdout(stream));
    co_await stream.async_shutdown(asio::as_tuple);  // vårt close_notify, oavsett vem som slutade först
}

auto usage(char const* prog) -> int {
    std::println(stderr, "användning: {} [--tls] [--ca FIL] [värd [port]]", prog);
    return 1;
}

int main(int argc, char** argv) {
    auto opts = Options{};
    auto positional = 0;
    for (auto i = 1; i < argc; ++i) {
        auto arg = std::string_view{argv[i]};
        if (arg == "--tls") opts.tls = true;
        else if (arg == "--ca" && i + 1 < argc) opts.ca = argv[++i];
        else if (arg.starts_with("-")) return usage(argv[0]);
        else if (positional++ == 0) opts.host = arg;
        else opts.port = arg;
    }

    auto ioc = asio::io_context{};
    auto status = 0;

    asio::co_spawn(ioc, chat(opts), [&status](std::exception_ptr e) {
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
}
