# Online play (Multi Play over the internet)

The original Multi Play uses DirectPlay over TCP/IP, which only works on a LAN or a VPN.
**Akuma Online** lets you play Multi Play over the internet with no VPN and no port forwarding.

How it works: everyone connects *out* to a small **relay** server. Players who use the same
relay, the same **network name** and the same **password** end up on one virtual LAN
(`10.77.x.y`) and see each other in the normal Multi Play session list. The game logic is not
changed: the launcher's DLL redirects the game's Winsock calls to the relay.

> Status: tested with two PCs on the same home network, and with one PC at home plus one game
> instance on a remote server, both through a relay in Brazil. Longer sessions and three or
> more players are not tested yet.

## Quick start (players)

1. Use a release that has the **Online** tab in `Jogar.exe` (the first release after 1.0.0).
2. Open `Jogar.exe` and go to the **Online** tab.
3. Tick **Play online through a relay**.
4. Pick a relay from the list (see [Public relays](#public-relays)) or type `host:port`.
5. Type a **network name** and a **password**. Everyone who wants to play together must use the
   same values. The first person to use a new name creates the network and sets its password.
6. Click **Test relay**. You should see a round-trip time in milliseconds.
7. Click **Play**. In the game: **Multi Play → TCP/IP**.
   - One player **creates** the session (host).
   - The others **search for sessions with the IP field empty** and join.

Pick the relay closest to *all* players: every packet goes player → relay → player.

## Public relays

| Name | Region | Address |
| --- | --- | --- |
| `brasil` | Brazil | `45.71.242.115:47700` |
| `singapore` | Singapore | `159.223.59.19:47700` |

They are listed in `relays.txt`, next to `Jogar.exe`, one per line: `name region host:port`.
Lines starting with `#` are comments. You can add your own (see
[Hosting a relay](Hosting-a-Relay.md)).

These servers are run on a best-effort basis by the project maintainer and may be restarted
or moved. Do not reuse a password you use anywhere else (see [Security](#security)).

## Without the launcher

The game reads, from its own folder or from environment variables:

- `relays.txt` — the relay list.
- `rede.txt` — which relay to use:
  ```
  relay=brasil
  rede=my-network
  senha=my-password
  apelido=my-nickname
  ```
- Environment variables override the file: `AKUMA_RELAY` (`host:port`), `AKUMA_NET`,
  `AKUMA_NETPASS`, `AKUMA_NICK`. `AKUMA_RELAY_OFF=1` ignores `rede.txt` and uses the normal
  Winsock (LAN/VPN).

Network names: letters, digits, space, `.`, `-`, `_` (up to 32 characters).

## Troubleshooting

| Symptom | What to check |
| --- | --- |
| **Test relay** fails | Wrong address or port, the relay is down, or a firewall blocks outbound TCP to that port |
| Session list is empty | Everyone must use the *same* relay, network name and password. Search with the IP field empty. Make sure the host already created the session |
| "Wrong password" in `relay-status.txt` | The network already exists with another password. Pick another name or ask the creator |
| Joins, then drops after a few seconds | Send us the logs below |

`relay-status.txt` (next to the game) shows the connection state and your virtual IP
(`connected: … ip=10.77.0.N`) or the last error.

### Logs for bug reports

Create empty files next to `AkumaHD.exe`:

- `winsocklog.enable` → `winsocklog.txt` (every Winsock call of the DirectPlay provider, with the
  first 160 bytes of each packet, and the relay events).
- `netlog.enable` → `netlog.txt` (every DirectPlay message the game sends and receives).

Delete the files to turn the logs off. They can be large; they never contain your password.

## Security

- **The network name and password are sent unencrypted** to the relay. Use a password you do
  not use anywhere else. TLS is planned.
- Anyone who knows the network name and password can join and send game traffic to you. The
  game trusts every client (HP, items and stats come from each player's save), so only share the
  password with people you trust.
- Only use relays you trust. The relay sees your public IP and can read and alter the traffic.
- The game itself only listens on `127.0.0.1` while Online is on, so no extra ports are opened
  on your PC.

## Limitations

- One virtual LAN per network name; networks are not shared between relays.
- Up to 16 players per network (default; the relay operator can change it).
- No encryption yet, and the relay has basic abuse limits only (see
  [Hosting a relay](Hosting-a-Relay.md#hardening)).

## How it works (short)

- `NetShim.c` (inside `AkumaHud.dll`) hooks the 22 Winsock functions that `dpwsockx.dll`, the
  DirectPlay TCP/IP provider, imports. The game's sockets become loopback sockets and a thread
  carries TCP streams and UDP datagrams to the relay.
- The discovery broadcast (`255.255.255.255:47624`) is delivered by the relay to every client
  of the network. On Windows a separate DirectPlay name-server process forwards it to the
  game's UDP socket (2350+); the shim plays that role.
- The provider writes the sender's address inside each message and replies to it. The shim
  learns which virtual IP sent the message and rewrites the destination.
- Details: [`docs/winsock-flow.md`](../winsock-flow.md) and [`relay/PROTOCOL.md`](../../relay/PROTOCOL.md).
