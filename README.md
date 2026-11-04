# asio-chatt-del-4

Demoprogram till artikeln **[Bygg en chattserver med Boost.Asio, del 4: TLS, UDP och graceful shutdown](https://www.ribomation.se/blog/2026/chattserver-boost-asio-del-4/)**
på [ribomation.se](https://www.ribomation.se/blog/).

Artikeln är den fjärde av fem om Boost.Asio. I [del 3](https://github.com/ribomation/asio-chatt-del-3) skrev vi
chattservern med coroutines, och syntaxen är klar. Den här delen handlar om det som saknas innan man vågar
köra den på riktigt: Ctrl-C dödar servern mitt i allt, trafiken går i klartext, och klienten måste veta
serverns adress. Så **signaler**, **TLS** och **UDP** (för discovery via broadcast), och till sist ett exempel på
att Asio inte bara är sockets: **asynkron filläsning** via io_uring. Allt skrivs med coroutines.

| Del | Innehåll | Repo |
|---|---|---|
| 1 | Grunderna och synkron I/O | [`asio-chatt-del-1`](https://github.com/ribomation/asio-chatt-del-1) |
| 2 | Asynkront med callbacks | [`asio-chatt-del-2`](https://github.com/ribomation/asio-chatt-del-2) |
| 3 | Asynkront med coroutines | [`asio-chatt-del-3`](https://github.com/ribomation/asio-chatt-del-3) |
| 4 | TLS, UDP och graceful shutdown | detta repo |
| 5 | Admin-UI med Boost.Beast och WebSocket | `asio-chatt-del-5` |

## Programmen

| Mapp | Program | Visar |
|---|---|---|
| [signals/](signals/) | `signals` | `signal_set` gör Ctrl-C till en asynkron operation: `co_await signals.async_wait()`. Mönstret *graceful shutdown*: sluta ta emot nytt, avsluta pågående arbete, låt `run()` returnera av sig själv. |
| [daytime-udp-server/](daytime-udp-server/) | `daytime-udp-server` | Daytime från del 1 över UDP: en `udp::socket` för alla klienter, `async_receive_from()` ger avsändaren, `async_send_to()` svarar. Ingen acceptor, ingen anslutning. |
| [daytime-udp-client/](daytime-udp-client/) | `daytime-udp-client` | Ett tomt datagram ut, ett svar in. UDP lovar ingen leverans, så svaret väntas med `\|\|` mot en timer, tre försök. |
| [echoserver-tls/](echoserver-tls/) | `echoserver-tls` | Echo-servern från del 3 med TLS: `ssl::context`, `ssl::stream<tcp::socket>` runt socketen, `async_handshake()` först och `async_shutdown()` sist. Loopen är oförändrad. |
| [chatserver/](chatserver/) | `chatserver` | Chatt steg 4: `Session<Stream>` för klartext eller TLS, `Participant` som gränssnitt mot `Room`, discovery på UDP 4712, och Ctrl-C som stänger snyggt: farväl, tömda köer, close_notify, 2 s tak. |
| [chatclient/](chatclient/) | `chatclient` | Del 3:s klient med TLS-verifiering mot serverns cert, och discovery via broadcast när ingen värd anges. |
| [readfile/](readfile/) | `readfile` | Asynkron filläsning med `stream_file` via io_uring: en coroutine per fil, alla filer samtidigt, en tråd. Samma `async_read_some()` som för en socket. |
| [client-simulator/](client-simulator/) | `chat-sim.py` | Simulatorn från del 1–3, nu med `--tls --ca FIL`, och ett meddelande per klient när servern stänger. |
| [certs/](certs/) | `make-cert.sh` | Självsignerat certifikat (EC P-256, CN=localhost, tio år). `server.crt` och `server.key` är incheckade så att repot fungerar direkt. |

## Förutsättningar

- Boost 1.92 (Asio är header-only, men länkar mot pthreads)
- OpenSSL (headers och bibliotek, `libssl-dev`) för `asio::ssl`. Testat med OpenSSL 4.0
- **liburing** för `readfile`: `sudo apt install liburing-dev`. Hittas med `pkg-config`, och används bara av det programmet
- GCC 16 och C++26 (`std::println`, `std::format`, coroutines)
- CMake 3.28 eller senare, Ninja
- `nc` (netcat) och `openssl s_client` som klienter, Python 3 för simulatorn
- `chatclient` använder `posix::stream_descriptor`, och `readfile` io_uring, så de bygger bara på Linux
  (klienten även macOS)

CMake-konfigurationen i [cmake/common.cmake](cmake/common.cmake) hittar Boost via handskrivna
find-moduler i `~/Libs/CMake-modules` (se [Kom igång med Boost](https://www.ribomation.se/blog/2026/kom-igang-med-boost/)),
och OpenSSL med CMakes inbyggda `find_package(OpenSSL)`. Den definierar två targets: `asio::common` (Boost + pthreads)
och `asio::ssl` (samma plus `OpenSSL::SSL` och `OpenSSL::Crypto`). Har du Boost någon annanstans räcker det att byta ut
raden `list(APPEND CMAKE_MODULE_PATH ...)` mot `-DBoost_ROOT=/sökväg/till/boost`, eller låta `find_package(Boost CONFIG)`
hitta en systeminstallation.

## Bygga

Alla program från projektroten, med presets (`debug`, `release` eller `tsan`):

```sh
cmake --preset release
cmake --build --preset release
```

Binärerna hamnar i `build/release/<mapp>/`. Ett program i taget, från dess egen mapp:

```sh
cd echoserver-tls
cmake -S . -B build -G Ninja
cmake --build build
./build/echoserver-tls 7000 ../certs
```

## Köra

| Program | Argument | Default | Klient/server |
|---|---|---|---|
| `signals` | | | Ctrl-C, eller `kill <pid>` |
| `daytime-udp-server` | `[port]` | 1313 | `daytime-udp-client`, eller `nc -4u localhost 1313` (se nedan) |
| `daytime-udp-client` | `[värd] [port]` | localhost 1313 | `daytime-udp-server` |
| `echoserver-tls` | `[port] [cert-mapp]` | 7000 certs | `openssl s_client -connect localhost:7000 -CAfile certs/server.crt -quiet` |
| `chatserver` | `[--tls\|--no-tls] [--certs MAPP] [port]` | no-tls, certs, 4711 | `chatclient`, `nc localhost 4711` (no-tls), `s_client` (tls), simulatorn |
| `chatclient` | `[--tls] [--ca FIL] [värd [port]]` | discovery, certs/server.crt | `chatserver` |
| `readfile` | `fil...` | | |

### Ctrl-C som asynkron operation

```sh
./build/release/signals/signals
```

```text
kör som pid 1508802, avbryt med Ctrl-C eller 'kill 1508802'
tick 1
tick 2
fick SIGINT, stänger snyggt (Ctrl-C igen dödar direkt)
ticker: avslutar pågående arbete (2 s)
ticker: klar
run() returnerade: nedstängd
```

Ett andra Ctrl-C under de två sekunderna dödar processen direkt, eftersom `signals.clear()` lämnat tillbaka
signalen till default-hanteringen.

### Daytime över UDP

Servern i en terminal, klienten i en annan:

```sh
./build/release/daytime-udp-server/daytime-udp-server
./build/release/daytime-udp-client/daytime-udp-client
```

```text
svar från 127.0.0.1:1313: 2026-11-04 19:27:40 CET
```

Utan server får klienten inget fel från `send_to`, bara tystnad. Den försöker tre gånger och ger upp:

```text
inget svar inom 2s (försök 1 av 3)
inget svar inom 2s (försök 2 av 3)
inget svar inom 2s (försök 3 av 3)
fel: 127.0.0.1:1313 svarar inte
```

Med `nc` behövs två saker: `-4` eftersom `localhost` annars blir `::1` och servern lyssnar på IPv4, och ett
`stdin` som hålls öppet en stund, annars avslutar `nc` innan svaret hunnit fram:

```sh
(echo; sleep 1) | nc -4u localhost 1313
```

Observera att `nc -u` utan avslut i stdin (`nc -4u localhost 1313 <<< ''`) kan bli hängande.

### Echo över TLS

```sh
./build/release/echoserver-tls/echoserver-tls
openssl s_client -connect localhost:7000 -CAfile certs/server.crt -quiet
```

`s_client -quiet` ekar det du skriver tillbaka, Ctrl-C eller Ctrl-D avslutar. Servern loggar handskakningen
och protokollversionen. Kör man `nc localhost 7000` i stället talar klienten klartext mot en server som väntar sig
TLS:

```text
echo-server (TLS) lyssnar på port 7000
ansluten: 127.0.0.1:51772
handskakning klar: 127.0.0.1:51772 (TLSv1.3)
nedkopplad: 127.0.0.1:51772
ansluten: 127.0.0.1:51786
fel mot 127.0.0.1:51786: wrong version number (SSL routines) [asio.ssl:167772427]
nedkopplad: 127.0.0.1:51786
```

### Chatt med TLS, discovery och snygg nedstängning

Servern med TLS i en terminal, klienten utan argument i en andra, `s_client` i en tredje:

```sh
./build/release/chatserver/chatserver --tls
./build/release/chatclient/chatclient
openssl s_client -connect localhost:4711 -CAfile certs/server.crt -quiet
```

Klienten hittar servern via broadcast. Svaret kommer från serverns adress på det lokala nätet, inte från
`127.0.0.1`, eftersom servern binder `0.0.0.0`:

```text
hittade servern på 192.168.68.62:4711 (tls)
ansluten till 192.168.68.62:4711 (TLSv1.3), skriv rader, Ctrl-D avslutar
*** 192.168.68.62:37868 anslöt
[192.168.68.62:37868] hej från klient 2
```

Ctrl-C i servern: acceptorn och discovery-socketen stängs, alla får `*** servern stänger`, varje session tömmer
sin skrivkö och skickar close_notify, och `run()` returnerar när sista sessionen stängt. Den som inte läser
inom två sekunder stängs hårt:

```text
chattserver (tls) lyssnar på port 4711, discovery på UDP 4712, pid 1490758
ansluten: 127.0.0.1:50640 (1 totalt, TLSv1.3)
ansluten: 127.0.0.1:50648 (2 totalt, TLSv1.3)
handskakning misslyckades: 127.0.0.1:50660: wrong version number (SSL routines) [asio.ssl:167772427]
ansluten: 127.0.0.1:50670 (3 totalt, TLSv1.3)
fick SIGINT, stänger snyggt (3 klienter, Ctrl-C igen dödar direkt)
listener: slutar ta emot anslutningar
nedkopplad: 127.0.0.1:50640 (2 kvar)
nedkopplad: 127.0.0.1:50648 (1 kvar)
stänger 1 som inte svarade inom 2s
nedkopplad: 127.0.0.1:50670 (0 kvar)
run() returnerade: nedstängd
```

Raden `handskakning misslyckades` är en `nc` som försökte i klartext. Klienten ser:

```text
*** servern stänger
(servern stängde)
```

Utan `--tls` är allt som i del 3, och `nc localhost 4711` fungerar som klient. Med explicit värd är klartext default,
`--tls` slår på TLS. Med fel certifikat som CA avbryts handskakningen:

```sh
./build/release/chatclient/chatclient --tls --ca /dev/null localhost
```

```text
fel: certificate verify failed (SSL routines) [asio.ssl:167772294]
```

### Fortfarande en tråd, även med TLS

Simulatorn med 200 TLS-klienter, och antalet trådar i servern medan den kör. Ctrl-C i servern mitt i ger
`servern stängde` från alla 200:

```sh
client-simulator/chat-sim.py --clients 200 --messages 5 --tls --ca certs/server.crt
ps -T -p $(pgrep -x chatserver) | tail -n +2 | wc -l
```

```text
1
```

### Asynkron filläsning

```sh
./build/release/readfile/readfile $(find . -name '*.cxx' -not -path './build/*') README.md finns-inte
```

```text
fel: finns-inte: open: No such file or directory [system:2]
      323 rader        13596 byte  ./chatserver/src/chatserver.cxx
       75 rader         2932 byte  ./echoserver-tls/src/echoserver-tls.cxx
      ...
     1135 rader        49148 byte  totalt, 9 filer på 1ms
```

Med några tusen filer (till exempel `find ~ -name '*.md'`) tar det under en sekund, och det var så buggen i
Fördjupning nedan hittades.

Siffrorna stämmer mot `wc -lc`. `strace -c` visar `io_uring_setup` och `io_uring_enter`, inga `read`/`pread`,
och `ps -T` visar en tråd.

## Fördjupning

Detaljer som artikeln bara snuddar vid:

- **`signal_set` och det andra Ctrl-C:t.** Så länge `signal_set` har en väntande `async_wait` fångar Asio signalen
  och levererar den till handlern. `signals.clear()` tar bort signalerna ur mängden och återställer
  default-hanteringen, så att nästa Ctrl-C dödar processen på vanligt sätt. Bra för en server som hängt sig i
  nedstängningen.
- **`run()` returnerar när sista I/O-objektet är klart.** Ingen `ioc.stop()` någonstans. Acceptorn stängs,
  discovery-socketen stängs, varje session stänger sin socket, och `signal_set` är tom efter `clear()`. Finns
  det inget pågående arbete returnerar `run()`.
- **Broadcast på localhost.** Ett datagram till `255.255.255.255:4712` når en socket bunden till `0.0.0.0:4712` på
  samma maskin, men svaret skickas från interfacets adress (`192.168.x.y`), inte `127.0.0.1`. Klienten ansluter
  dit, och det fungerar eftersom acceptorn också lyssnar på `0.0.0.0`. Klienten behöver `set_option(broadcast(true))`,
  annars ger `send_to` "Permission denied".
- **En UDP-socket ser inte ICMP.** Finns ingen server svarar kärnan med ICMP port unreachable, men en socket som inte
  är `connect`:ad får aldrig felet. Därav tystnaden, och timern i klienten.
- **`ssl::stream` är en adapter.** `stream.next_layer()` och `lowest_layer()` är socketen, och det är den man
  frågar om `remote_endpoint()` och stänger med `close()`. `native_handle()` är OpenSSL:s `SSL*`, för till exempel
  `SSL_get_version()`.
- **Tre sätt för en TLS-klient att försvinna.** `eof` betyder att klienten skickade close_notify och vi kan svara
  med `async_shutdown()`. `ssl::error::stream_truncated` betyder att klienten stängde TCP-anslutningen utan
  close_notify (`s_client` med Ctrl-C gör så), och då finns ingen att skicka close_notify till. Övriga fel loggas.
- **Varför `async_shutdown()` tar tid.** Den skickar vårt close_notify och väntar på klientens. En klient som
  inte läser svarar aldrig, och därför har `Room::shutdown()` ett tak på två sekunder, varefter `close()` stänger
  socketen under den väntande operationen.
- **Ingen värdnamnskontroll i klienten.** `verify_peer` kontrollerar att serverns certifikat är signerat av en
  betrodd CA, och enda CA är `server.crt` självt. Discovery ger en IP-adress som inte behöver stå i certet, så
  `host_name_verification` är utelämnad. I produktion gör man tvärtom: systemets CA-lager plus kontroll av
  värdnamnet. Python-simulatorn har kontrollen på och skickar därför `server_hostname="localhost"`.
- **`Participant` ger `Room` en typ att hålla.** `Session<tcp::socket>` och `Session<tls_stream>` är två olika klasser,
  och `Room` ska kunna hålla vilken som i samma mängd. Samma idé som `chat_participant` i Asios chat-exempel. Priset
  är virtuella anrop för `deliver()`, vilket är irrelevant jämfört med en `async_write`.
- **`if constexpr` för TLS.** Tre ställen i `Session<Stream>` vet om strömmen är TLS: handskakningen i `start()`,
  `async_shutdown()` i `writer()`, och loggraden. Allt annat är del 3:s kod oförändrad, och kompileras två gånger.
- **Snygg nedstängning i detalj.** `Room::shutdown()` sänder farvälet, sätter `closing` i varje session och väntar
  på en timer som `leave()` avbryter när sista deltagaren gått. Samma väckarklocka som `writer()` använder.
  `writer()` avslutar när kön är tom och `closing` är satt, i stället för att vänta på mer. Under nedstängningen
  tystas `*** x kopplade ned`, så att farvälet är det sista klienterna ser.
- **io_uring bara för `readfile`.** `BOOST_ASIO_HAS_IO_URING` definieras på det targetet, inte i `common.cmake`.
  Utan `BOOST_ASIO_DISABLE_EPOLL` skulle sockets ändå gå via epoll och bara filer via io_uring, men se nästa punkt.
- **En bugg i Boost 1.92 med epoll + io_uring.** Med epoll som reaktor och io_uring vid sidan om signaleras
  io_uring-completions via en eventfd som epoll bevakar edge-triggered. Asio läser eventfd:n och tömmer sedan
  högst 128 completions (`complete_batch_size`). Blir fler än 128 klara i en klump ligger resten kvar utan ny
  väckning, och `run()` hänger i `epoll_wait`. Det inträffade vid några hundra filer. Workaround:
  `BOOST_ASIO_HAS_IO_URING_AS_DEFAULT`, som gör io_uring till reaktor för hela programmet och tar completions via
  `io_uring_wait_cqe`. Verifierat med 10 000 filer.
- **Blockvis, inte radvis.** `readfile` läser 64 KB i taget med `async_read_some()` och räknar radslut med
  `std::ranges::count`. En första version med `async_read_until()` per rad var 20–30 gånger långsammare.
- **Ingen mutex runt `total`.** Varje `co_spawn` får en handler som summerar in filens siffror. Alla handlers körs
  inne i `run()` i en tråd, så summan behöver inget lås. Signaturen för en `awaitable<T>` är
  `void(std::exception_ptr, T)`.

## Relaterade artiklar

- [Bygg en chattserver med Boost.Asio, del 1](https://www.ribomation.se/blog/2026/chattserver-boost-asio-del-1/)
- [Bygg en chattserver med Boost.Asio, del 2](https://www.ribomation.se/blog/2026/chattserver-boost-asio-del-2/)
- [Bygg en chattserver med Boost.Asio, del 3](https://www.ribomation.se/blog/2026/chattserver-boost-asio-del-3/)
- [Asynkron filläsning med C++20-coroutines](https://www.ribomation.se/blog/2026/asynkron-fillasning-med-c++20-coroutines/)
- [Kom igång med Boost](https://www.ribomation.se/blog/2026/kom-igang-med-boost/)
