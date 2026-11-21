# Client–relay protocol (version 1)

TCP, default port 47700. Frame: `u16 length` (big-endian, number of bytes that follow, including
the type, at most 16383) + `u8 type` + payload. Integers are in network order; IPs are 4 raw
bytes.

| Type | Direction | Payload |
| --- | --- | --- |
| `0x01` HELLO | C→S | `u8 version (1)`, `u8 n` + network, `u8 n` + password, `u8 n` + nickname (each ≤ 32) |
| `0x81` WELCOME | S→C | `virtual ip[4]`, `u16 players in the network` |
| `0x8F` ERROR | S→C | `u8 code`, text. 1 protocol, 2 password, 3 no free networks, 4 network full, 5 invalid name, 6 rate limit |
| `0x02` UDP | C→S | `u16 source port`, `dest ip[4]`, `u16 dest port`, data. A destination ending in `.255` is the network broadcast (it also reaches the sender, like on a LAN) |
| `0x82` UDP | S→C | `source ip[4]`, `u16 source port`, `u16 dest port`, data |
| `0x03` CONNECT | C→S | `u32 sid`, `dest ip[4]`, `u16 dest port`, `u16 source port` |
| `0x83` INCOMING | S→C | `initiator ip[4]`, `u32 sid`, `u16 source port`, `u16 dest port` |
| `0x04` DATA / `0x05` CLOSE | both | `initiator ip[4]`, `u32 sid`, `u8 dir`, [data]. `dir` 0 = initiator to target, 1 = target to initiator |
| `0x06` PING / `0x86` PONG | C→S / S→C | `u32` token (clients send one every 15 s; 60 s without traffic drops the client) |

- A TCP stream is identified by `(initiator ip, sid)`. A CONNECT to an IP nobody owns is
  answered with `CLOSE dir=1`.
- Virtual IP: `10.77.<network index>.<1..253>`. A network is created by the first HELLO (the
  first player's password applies) and disappears when it empties.
- Limits: 16 per network (`-n`), 256 clients (`-m`), 64 networks, 2 MB/s per client, 2 MB output
  queue per client.
- **No encryption yet**: network name and password travel in clear text.
