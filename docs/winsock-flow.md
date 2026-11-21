# DirectPlay network flow (measured)

How the game's DirectPlay TCP/IP provider (`dpwsockx.dll`) uses Winsock, from logs of two game
instances (`winsocklog.enable`) and from static analysis. Message-level protocol:
[netcode.md](netcode.md) (Portuguese). Everything below was observed unless marked
**hypothesis**.

## Where to hook

- `dpwsockx.dll` is not loaded with the game. `dplayx.dll` loads it with `LoadLibrary` when the
  game picks the TCP/IP provider.
- It imports 26 functions from `ws2_32` **by ordinal** (not by name; the import table in the
  file looks like names, but the loader's table has the ordinal flag). The IAT is already
  resolved when `LdrRegisterDllNotification` reports the load, so it is enough to replace each
  IAT entry whose value equals the address of the real `ws2_32` function. 22 functions are
  hooked; `htons`, `ntohs`, `__WSAFDIsSet` and `WSAGetLastError` are left alone.

## Ports

| Use | Port |
| --- | --- |
| Game TCP (`bind 0.0.0.0`, `listen`) | 2300; if busy, 2301, 2302… |
| Game UDP (`bind 0.0.0.0`) | 2350; if busy, 2351… |
| Session discovery | UDP broadcast to `255.255.255.255:47624` |

The provider toggles `SO_REUSEADDR`, sets `SO_KEEPALIVE` and `TCP_NODELAY` on connecting sockets,
uses non-blocking connects followed by `select`, and closes with `SO_LINGER`.

## Session discovery (searching with an empty IP field)

1. The searching client opens an ephemeral UDP socket (`SO_BROADCAST`) and sends 52 bytes to
   `255.255.255.255:47624`, then closes it. It repeats this every few seconds.
2. On Windows, a process other than the game listens on 47624 (**hypothesis**: the DirectPlay
   name server) and forwards the datagram to the hosting game's UDP socket (2350+). The game
   sees it coming from `127.0.0.1:<ephemeral>` with the token changed from `b0fa` to `b0ca`.
3. The host answers with a **TCP connection to the searching client's listener** (2300+) and
   sends a 116-byte reply (command 1, session name in UTF-16). The searching client then
   connects to the host's TCP port to join.

Message layout (TCP: a 4-byte header read first, then the body; UDP: header at the start of the
datagram): `u32 size | 0xFAB0<<16`, a 16-byte `SOCKADDR`, the signature `play`, `u16` command,
`u16` version (`0x000e`), then the payload. The discovery question carries the application GUID
`{9b41cb61-4db7-11d2-97de-d81308c10000}`. After joining, the game also sends 36-byte UDP
messages between its 2350+ sockets (commands `0x16`/`0x17`; **hypothesis**: keepalive/clock).

## The address inside the header

The provider writes the sender's address in the 16-byte `SOCKADDR` and the receiver replies to
it, not to the datagram's source address.

- If the sender has the same machine name as the receiver (two instances on one PC), the
  receiver rewrites the field in place to its own LAN IP and TCP port 2300+ (observed:
  `0200 08fc c0a800c7` = 192.168.0.199:2300).
- If the sender is another machine, the field is left as it came, and it is garbage as far as
  routing goes (observed: `100.69.88.153:37576`, constant per machine except the port).
  The sender's machine name is visible in the last bytes of the field (`sin_zero`).

On a real LAN the sender's real address must come from somewhere (**not understood**); on the
virtual LAN neither the real IP nor that garbage is reachable.

## What the shim does about it

- Sockets of the game are real loopback sockets on ephemeral ports; the game sees virtual ports
  through `bind`/`getsockname`, so two instances on one PC do not clash on 2300/2350.
- A datagram for port 47624 is delivered to every local UDP socket whose virtual port is in
  2350–2399 (the name-server's job), with the sender's virtual address as source.
- After each `recv`/`recvfrom` it remembers the virtual sender, and on the next `connect` or
  `sendto` to a non-virtual address it substitutes the virtual IP: first by a learned
  *real IP → virtual IP* table (peeking the receive buffer after the provider decoded it), then
  by "the last virtual sender in the last 5 seconds". If the port is not in 2300–2349 (TCP) or
  2350–2399 (UDP) it uses 2300 / 2350.
- **Known weakness**: with three or more players answering at once, the 5-second guess can pick
  the wrong peer. The machine name in `sin_zero` could be used as a key.
