# Hosting a relay

The relay (`relay/akuma-relay.c`) is one small C program: one thread, a `poll` loop, no
dependencies. It only does one thing: **online access**. It never runs game logic, executes
commands, opens files or connects out.

- Listens on **TCP 47700** (change with `-p`).
- Groups clients into *networks* (name + password) and gives each one a virtual IP
  `10.77.<network>.<n>`.
- Forwards TCP streams between clients of the same network and emulates the UDP broadcast of
  the session discovery.

A very small VPS is enough (tens of MB of RAM, little traffic). Put it close to your players.

## Build

With the Zig bundled in the project (`.work/zig`, see the main README):

```powershell
.\relay\build.ps1
```

This writes `relay\bin\` with: `akuma-relay-win64.exe`, `akuma-relay-win32.exe`,
`akuma-relay-linux-x86_64` and `akuma-relay-linux-aarch64` (static, no libc needed).

Or by hand:

```
zig cc -target x86_64-linux-musl -O2 -s -o akuma-relay relay/akuma-relay.c
zig cc -target x86_64-windows-gnu -O2 -s -o akuma-relay.exe relay/akuma-relay.c -lws2_32
```

## Options

```
akuma-relay [-p port] [-m max_clients] [-n max_per_network] [-v]
```

| Option | Default | Meaning |
| --- | --- | --- |
| `-p` | 47700 | TCP port |
| `-m` | 256 | maximum simultaneous clients |
| `-n` | 16 | maximum players per network |
| `-v` | off | log every UDP datagram and TCP stream (very chatty) |

The relay logs to stdout: networks created/removed, players joining/leaving, wrong passwords.

## Linux (Ubuntu) with systemd

From a Linux shell (or WSL on Windows), upload the binary:

```bash
scp relay/bin/akuma-relay-linux-x86_64 root@YOUR.SERVER.IP:/root/akuma-relay
```

On the server:

```bash
sudo useradd --system --no-create-home --shell /usr/sbin/nologin akuma
sudo install -m 755 /root/akuma-relay /usr/local/bin/akuma-relay
```

```bash
sudo tee /etc/systemd/system/akuma-relay.service >/dev/null <<'EOF'
[Unit]
Description=Akuma relay
After=network.target

[Service]
User=akuma
ExecStart=/usr/local/bin/akuma-relay -p 47700
Restart=always
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
PrivateTmp=yes

[Install]
WantedBy=multi-user.target
EOF
```

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now akuma-relay
```

| Task | Command |
| --- | --- |
| Status | `systemctl status akuma-relay` |
| Follow the log | `journalctl -u akuma-relay -f` |
| Restart | `sudo systemctl restart akuma-relay` |
| Stop | `sudo systemctl stop akuma-relay` |

To update: upload the new binary, run `sudo install -m 755 /root/akuma-relay
/usr/local/bin/akuma-relay` and restart.

If the service fails with exit code 1 right away, read `journalctl -u akuma-relay -n 20`. The
usual cause is another instance (for example one started with `nohup`) still holding the
port: `ss -tlnp | grep 47700`, then `pkill akuma-relay` and
`sudo systemctl reset-failed akuma-relay && sudo systemctl start akuma-relay`.

### Quick and dirty (no systemd)

```bash
chmod +x /root/akuma-relay
nohup /root/akuma-relay -p 47700 > /root/relay.log 2>&1 &
tail -f /root/relay.log
```

It will not restart by itself after a reboot, and it runs as the user who started it. Prefer
systemd and a dedicated user.

## Firewall

Open **TCP 47700** and nothing else (plus SSH):

```bash
sudo ufw allow 22/tcp
sudo ufw allow 47700/tcp
sudo ufw enable
```

Allow `22` first or you will lock yourself out. On DigitalOcean, AWS and similar, also open
the port in the provider's cloud firewall / security group.

## Windows

Run `akuma-relay-win64.exe -v` in a terminal and allow TCP 47700 in Windows Firewall. Fine for
tests; use a Linux VPS for a public relay.

## Add it to the clients

Players type `host:port` in the Online tab, or you add a line to `relays.txt`:

```
singapore  SG  159.223.59.19:47700
```

`relays.txt` ships in the patch next to `Jogar.exe`.

## Hardening

What the relay does today:

- Bounds-checks every length it reads (names ≤ 32 bytes, frames ≤ 16 KB) and drops a client on
  any malformed frame.
- Idle timeout (60 s without traffic), per-client rate limit (2 MB/s) and output queue cap (2 MB).
- At most 64 networks and `-m` clients.

What it does **not** do yet:

- No encryption (the password and all game traffic travel in clear text).
- No per-IP connection limit and no short deadline for the first message, so a flood of
  connections can fill the client slots (denial of service).
- Anyone can create a network. There is no allow-list.
- It has not been fuzzed or audited. Treat a bug in C code that parses internet input as a
  possible remote-code-execution risk, and contain it: run as an unprivileged user (see the
  systemd unit), keep the firewall closed except for the relay port and SSH, use key-only SSH
  and keep the machine updated.

Do not run the game itself on the relay machine without the relay mode; the game's own
DirectPlay ports (2300 and 47624) must stay closed to the internet.
