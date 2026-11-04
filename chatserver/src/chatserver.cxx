// Chattserver, steg 4: TLS, discovery över UDP och snygg nedstängning.
//
//   listener():           accept-loop, skapar en Session<Stream> per klient
//   Session<Stream>:      steg 3:s Session som klassmall, Stream är tcp::socket
//                         (--no-tls, default) eller ssl::stream<tcp::socket> (--tls).
//                         start() gör handskakningen, writer() gör async_shutdown.
//   Participant:          det Room ser av en Session: name(), deliver(), shutdown(), close()
//   Room:                 mängden av deltagare, broadcast, och shutdown() som tömmer alla
//                         köer med ett tak på 2 s
//   discovery():          UDP-socket på port 4712 som svarar "CHAT <port> tls|no-tls"
//                         på "CHAT?", så att en klient hittar servern med broadcast
//   shutdown_on_signal(): Ctrl-C: stäng acceptor och discovery, säg hej då, töm köer,
//                         stäng. run() returnerar av sig själv, inget ioc.stop().
//
// reader() och writer() är steg 3, med en closing-flagga i writer(): när kön är
// tom och servern stänger avslutar den, i stället för att vänta på mer.
//
// Testa med: chatclient, nc localhost 4711 (no-tls), openssl s_client -connect
// localhost:4711 -CAfile certs/server.crt -quiet (tls), eller client-simulator/chat-sim.py
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <deque>
#include <format>
#include <memory>
#include <print>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <unistd.h>

namespace asio = boost::asio;
namespace ssl = asio::ssl;
using tcp = asio::ip::tcp;
using udp = asio::ip::udp;
using namespace std::chrono_literals;
using asio::awaitable;
using asio::co_spawn;
using asio::detached;
using tls_stream = ssl::stream<tcp::socket>;

constexpr auto max_queue = std::size_t{1000};  // tak för skrivkön, se del 2
constexpr auto discovery_port = static_cast<unsigned short>(4712);
constexpr auto shutdown_cap = 2s;  // så länge får klienterna på sig att läsa farvälet

auto to_string(tcp::endpoint const& ep) -> std::string {
    return std::format("{}:{}", ep.address().to_string(), ep.port());
}

auto strip_eol(std::string_view s) -> std::string_view {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

// Det Room behöver veta om en session. Room slipper då veta om strömmen är
// TLS eller inte, och kan hålla Session<tcp::socket> och Session<tls_stream>
// i samma mängd. Samma idé som chat_participant i Asios chat-exempel.
struct Participant {
    virtual ~Participant() = default;
    virtual auto name() const -> std::string const& = 0;
    virtual void deliver(std::string msg) = 0;
    virtual void shutdown() = 0;  // snygg: töm kön, säg hej då på TLS-nivå, stäng
    virtual void close() = 0;     // hård: stäng nu
};
using ParticipantPtr = std::shared_ptr<Participant>;

class Room {
    std::set<ParticipantPtr> participants;
    asio::steady_timer drained;  // väcks när sista deltagaren lämnat
    bool closing = false;

public:
    explicit Room(asio::any_io_executor ex) : drained{ex} {}

    void join(ParticipantPtr p) { participants.insert(std::move(p)); }
    void leave(ParticipantPtr const& p) {
        participants.erase(p);
        if (participants.empty()) drained.cancel();
    }
    auto size() const -> std::size_t { return participants.size(); }

    // from == nullptr betyder att servern själv är avsändare.
    void broadcast(std::string const& msg, ParticipantPtr const& from) {
        if (closing) return;  // under nedstängning är farvälet det sista som sägs
        for (auto const& p : participants) {
            if (p != from) p->deliver(msg);
        }
    }

    // Snygg nedstängning: farväl till alla, sedan får var och en tömma sin kö
    // och stänga. Den som inte hunnit inom taket stängs hårt.
    auto shutdown() -> awaitable<void> {
        broadcast("*** servern stänger\n", nullptr);
        closing = true;
        for (auto const& p : participants) p->shutdown();

        drained.expires_after(shutdown_cap);
        co_await drained.async_wait(asio::as_tuple);  // operation_aborted = alla lämnade i tid

        if (!participants.empty()) std::println("stänger {} som inte svarade inom {}", size(), shutdown_cap);
        for (auto const& p : std::set{participants}) p->close();  // close() ändrar mängden
    }
};

template <typename Stream>
class Session : public Participant, public std::enable_shared_from_this<Session<Stream>> {
    using super = std::enable_shared_from_this<Session<Stream>>;
    static constexpr auto is_tls = std::is_same_v<Stream, tls_stream>;

    Stream stream;
    asio::steady_timer timer;  // väckarklocka för writer(), går aldrig av
    Room& room;
    std::string peer;
    std::string read_buf;
    std::deque<std::string> queue;
    std::size_t dropped = 0;
    bool closing = false;

public:
    Session(Stream stream, Room& room)
        : stream{std::move(stream)}, timer{this->stream.get_executor()}, room{room},
          peer{to_string(this->stream.lowest_layer().remote_endpoint())} {  // lowest_layer() är socketen
        timer.expires_at(asio::steady_timer::time_point::max());
    }

    auto name() const -> std::string const& override { return peer; }

    // Handskakning (bara TLS), sedan in i rummet och igång med de två
    // coroutinerna från steg 3. Lambdorna håller self, så sessionen lever så
    // länge någon av dem kör.
    auto start() -> awaitable<void> {
        auto self = super::shared_from_this();
        if constexpr (is_tls) {
            try {
                co_await stream.async_handshake(ssl::stream_base::server);
            } catch (boost::system::system_error const& e) {
                std::println("handskakning misslyckades: {}: {}", peer, e.what());
                co_return;  // sessionen dör med self
            }
        }
        room.join(self);
        std::println("ansluten: {} ({} totalt, {})", peer, room.size(), protocol());
        room.broadcast(std::format("*** {} anslöt\n", peer), self);

        co_spawn(stream.get_executor(), [self] { return self->reader(); }, detached);
        co_spawn(stream.get_executor(), [self] { return self->writer(); }, detached);
    }

    void deliver(std::string msg) override {
        if (queue.size() >= max_queue) {
            queue.pop_front();
            ++dropped;
        }
        queue.push_back(std::move(msg));
        timer.cancel_one();
    }

    // Snygg: writer() ser flaggan när kön är tom, och avslutar i stället för att vänta.
    void shutdown() override {
        closing = true;
        timer.cancel_one();
    }

    // Hård, och idempotent: båda coroutinerna hamnar här. Den första stänger
    // socketen, vilket väcker den andra med operation_aborted.
    void close() override {
        auto& socket = stream.lowest_layer();
        if (!socket.is_open()) return;
        auto ec = boost::system::error_code{};
        socket.close(ec);
        timer.cancel();

        auto self = super::shared_from_this();
        room.leave(self);
        std::println("nedkopplad: {} ({} kvar{})", peer, room.size(),
                     dropped ? std::format(", kastade {} meddelanden", dropped) : "");
        room.broadcast(std::format("*** {} kopplade ned\n", peer), self);
    }

private:
    auto protocol() -> std::string {
        if constexpr (is_tls) return SSL_get_version(stream.native_handle());
        else return "klartext";
    }

    auto reader() -> awaitable<void> {
        try {
            for (;;) {
                auto n = co_await asio::async_read_until(stream, asio::dynamic_buffer(read_buf), '\n');
                auto line = strip_eol(std::string_view{read_buf}.substr(0, n));
                room.broadcast(std::format("[{}] {}\n", peer, line), super::shared_from_this());
                read_buf.erase(0, n);
            }
        } catch (boost::system::system_error const&) {
            // eof, stream_truncated (TLS-klient som stängde utan close_notify),
            // eller operation_aborted när writer() stängt socketen
        }
        close();
    }

    auto writer() -> awaitable<void> {
        try {
            while (stream.lowest_layer().is_open()) {
                if (queue.empty()) {
                    if (closing) break;  // servern stänger och kön är tömd: klart
                    co_await timer.async_wait(asio::as_tuple);
                    continue;
                }
                auto msg = std::move(queue.front());
                queue.pop_front();
                co_await asio::async_write(stream, asio::buffer(msg));
            }
            if constexpr (is_tls) {
                // Vårt close_notify, och vänta på klientens. Felet är ointressant:
                // antingen svarade klienten, eller så stänger close() nedan.
                if (closing) co_await stream.async_shutdown(asio::as_tuple);
            }
        } catch (boost::system::system_error const&) {
            // skrivfel, eller operation_aborted när reader() stängt socketen
        }
        close();
    }
};

template <typename Stream>
void start_session(Stream stream, Room& room) {
    auto ex = stream.get_executor();
    auto session = std::make_shared<Session<Stream>>(std::move(stream), room);
    co_spawn(ex, [session] { return session->start(); }, detached);
}

// ctx == nullptr betyder klartext. Acceptorn stängs av shutdown_on_signal(),
// vilket ger operation_aborted här och avslutar loopen.
auto listener(tcp::acceptor& acceptor, Room& room, ssl::context* ctx) -> awaitable<void> {
    try {
        for (;;) {
            auto socket = co_await acceptor.async_accept();
            if (ctx)
                start_session(tls_stream{std::move(socket), *ctx}, room);
            else
                start_session(std::move(socket), room);
        }
    } catch (boost::system::system_error const& e) {
        if (e.code() != asio::error::operation_aborted) throw;
    }
    std::println("listener: slutar ta emot anslutningar");
}

// Svarar på "CHAT?" med "CHAT <port> tls|no-tls", så att en klient på samma
// nät hittar servern med en broadcast. Avsändarens adress följer med varje
// datagram, och det är den klienten sedan ansluter till.
auto discovery(udp::socket& socket, unsigned short port, bool tls) -> awaitable<void> {
    auto reply = std::format("CHAT {} {}\n", port, tls ? "tls" : "no-tls");
    auto buf = std::array<char, 64>{};
    auto sender = udp::endpoint{};
    try {
        for (;;) {
            auto n = co_await socket.async_receive_from(asio::buffer(buf), sender);
            if (strip_eol({buf.data(), n}) != "CHAT?") continue;
            std::println("discovery: fråga från {}:{}", sender.address().to_string(), sender.port());
            co_await socket.async_send_to(asio::buffer(reply), sender);
        }
    } catch (boost::system::system_error const& e) {
        if (e.code() != asio::error::operation_aborted) throw;
    }
}

auto shutdown_on_signal(asio::signal_set& signals, tcp::acceptor& acceptor, udp::socket& discovery, Room& room)
    -> awaitable<void> {
    auto signo = co_await signals.async_wait();
    std::println("fick {}, stänger snyggt ({} klienter, Ctrl-C igen dödar direkt)",
                 signo == SIGINT ? "SIGINT" : "SIGTERM", room.size());
    signals.clear();     // nästa Ctrl-C får default-beteendet
    acceptor.close();    // inga nya klienter
    discovery.close();   // och inga nya discovery-svar
    co_await room.shutdown();
}

auto usage(char const* prog) -> int {
    std::println(stderr, "användning: {} [--tls|--no-tls] [--certs DIR] [port]", prog);
    return 1;
}

int main(int argc, char** argv) try {
    auto tls = false;
    auto certs = std::string{"certs"};
    auto port = static_cast<unsigned short>(4711);
    for (auto i = 1; i < argc; ++i) {
        auto arg = std::string_view{argv[i]};
        if (arg == "--tls") tls = true;
        else if (arg == "--no-tls") tls = false;
        else if (arg == "--certs" && i + 1 < argc) certs = argv[++i];
        else if (!arg.starts_with("-")) port = static_cast<unsigned short>(std::stoi(argv[i]));
        else return usage(argv[0]);
    }

    auto ctx = ssl::context{ssl::context::tls_server};
    if (tls) {
        ctx.use_certificate_chain_file(certs + "/server.crt");
        ctx.use_private_key_file(certs + "/server.key", ssl::context::pem);
    }

    auto ioc = asio::io_context{};
    auto room = Room{ioc.get_executor()};
    auto acceptor = tcp::acceptor{ioc, tcp::endpoint{tcp::v4(), port}};
    auto discovery_socket = udp::socket{ioc, udp::endpoint{udp::v4(), discovery_port}};  // 0.0.0.0: tar emot broadcast
    auto signals = asio::signal_set{ioc, SIGINT, SIGTERM};
    std::println("chattserver ({}) lyssnar på port {}, discovery på UDP {}, pid {}",
                 tls ? "tls" : "no-tls", port, discovery_port, getpid());

    co_spawn(ioc, listener(acceptor, room, tls ? &ctx : nullptr), detached);
    co_spawn(ioc, discovery(discovery_socket, port, tls), detached);
    co_spawn(ioc, shutdown_on_signal(signals, acceptor, discovery_socket, room), detached);
    ioc.run();  // returnerar när inget arbete finns kvar: inga sessioner, inga sockets, ingen signal_set
    std::println("run() returnerade: nedstängd");
} catch (std::exception const& e) {
    std::println(stderr, "fel: {}", e.what());
    return 1;
}
