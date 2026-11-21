// NetShim: Winsock do DirectPlay dentro do processo do jogo.
//
// O provedor TCP/IP do DirectPlay (dpwsockx.dll) importa so 22 funcoes BSD simples do ws2_32.
// Quando a DLL carrega (LdrRegisterDllNotification), a IAT dela e trocada por estas funcoes.
//
//   1. Log (fase 0): com winsocklog.enable (ou AKUMA_WINSOCKLOG=1) ao lado do exe, cada chamada
//      e gravada em winsocklog.txt com argumentos, resultado, erro e os primeiros bytes.
//   2. Relay (fase 1): com rede.txt + relays.txt (ou AKUMA_RELAY=host:porta) o jogo passa a ver
//      uma LAN virtual 10.77.x.y. Todos os sockets do jogo ficam em 127.0.0.1 (portas efemeras),
//      uma thread leva fluxos TCP e UDP ao relay. A logica do jogo nao muda. Veja relay/PROTOCOLO.md.
//
// Sem a flag e sem configuracao de relay, as chamadas passam direto (custo: um teste por chamada).

#define WIN32_LEAN_AND_MEAN
#define FD_SETSIZE 256
#include <winsock2.h>
#include <windows.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// Log

static HANDLE g_log = INVALID_HANDLE_VALUE;
static int g_log_on;
static DWORD g_t0;
static DWORD g_log_bytes;
static CRITICAL_SECTION g_logcs;
static CRITICAL_SECTION g_cs;      // tabelas de sockets, proxies
static CRITICAL_SECTION g_sendcs;  // escrita no socket do relay
static char g_dir[MAX_PATH];

static void exe_path(const char *name, char *out, int cap)
{
    lstrcpynA(out, g_dir, cap);
    lstrcatA(out, name);
}

static void wlog(const char *fmt, ...)
{
    if (!g_log_on) return;
    char line[1800];
    va_list ap;
    int n = wsprintfA(line, "%lu t%lu ", GetTickCount() - g_t0, GetCurrentThreadId());
    va_start(ap, fmt);
    n += wvsprintfA(line + n, fmt, ap);
    va_end(ap);
    if (n > 1790) n = 1790;
    line[n++] = '\n';
    EnterCriticalSection(&g_logcs);
    if (g_log_bytes < 64u * 1024 * 1024) {
        DWORD w;
        WriteFile(g_log, line, (DWORD)n, &w, NULL);
        g_log_bytes += (DWORD)n;
    }
    LeaveCriticalSection(&g_logcs);
}

static const char *hexs(const void *p, int n, char *out)
{
    static const char d[] = "0123456789abcdef";
    const unsigned char *b = (const unsigned char *)p;
    if (n > 160) n = 160;
    int k = 0;
    for (int i = 0; i < n; ++i) { out[k++] = d[b[i] >> 4]; out[k++] = d[b[i] & 15]; }
    out[k] = 0;
    return out;
}

static const char *addrs(const struct sockaddr *a, int len, char *out)
{
    if (!a) { lstrcpyA(out, "(null)"); return out; }
    if (a->sa_family != AF_INET || len < (int)sizeof(struct sockaddr_in)) {
        wsprintfA(out, "af=%d len=%d", a->sa_family, len);
        return out;
    }
    const struct sockaddr_in *in = (const struct sockaddr_in *)a;
    const unsigned char *ip = (const unsigned char *)&in->sin_addr;
    wsprintfA(out, "%u.%u.%u.%u:%u", ip[0], ip[1], ip[2], ip[3], (unsigned)ntohs(in->sin_port));
    return out;
}

// ---------------------------------------------------------------------------
// Configuracao do relay

static int g_relay_on;
static char g_relay_host[128];
static int g_relay_port = 47700;
static char g_net[40], g_pass[40], g_nick[40];
static volatile uint32_t g_vip;          // IP virtual, ordem de rede
static HANDLE g_vip_event;
static volatile LONG g_started;

static int read_text(const char *name, char *buf, int cap)
{
    char path[MAX_PATH];
    exe_path(name, path, sizeof(path));
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    DWORD n = 0;
    ReadFile(h, buf, (DWORD)cap - 1, &n, NULL);
    CloseHandle(h);
    buf[n] = 0;
    return (int)n;
}

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n' || (unsigned char)*s == 0xEF || (unsigned char)*s == 0xBB || (unsigned char)*s == 0xBF) ++s;
    char *e = s + lstrlenA(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
    return s;
}

static void set_host_port(const char *spec)
{
    char tmp[160];
    lstrcpynA(tmp, spec, sizeof(tmp));
    char *colon = strrchr(tmp, ':');
    if (colon) { *colon = 0; g_relay_port = atoi(colon + 1); }
    lstrcpynA(g_relay_host, tmp, sizeof(g_relay_host));
}

// relays.txt: nome regiao host:porta  (uma linha por relay, # comenta)
// rede.txt:   relay=<nome do relays.txt ou host:porta>, rede=<nome>, senha=<senha>, apelido=<nome>
static void load_config(void)
{
    static char text[8192];
    char relay_pick[160] = "";
    lstrcpyA(g_nick, "jogador");
    if (read_text("rede.txt", text, sizeof(text))) {
        for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
            char *l = trim(line);
            if (*l == '#' || !*l) continue;
            char *eq = strchr(l, '=');
            if (!eq) continue;
            *eq = 0;
            char *k = trim(l), *v = trim(eq + 1);
            if (!lstrcmpiA(k, "relay")) lstrcpynA(relay_pick, v, sizeof(relay_pick));
            else if (!lstrcmpiA(k, "rede")) lstrcpynA(g_net, v, sizeof(g_net));
            else if (!lstrcmpiA(k, "senha")) lstrcpynA(g_pass, v, sizeof(g_pass));
            else if (!lstrcmpiA(k, "apelido")) lstrcpynA(g_nick, v, sizeof(g_nick));
        }
    }
    char env[160];
    if (GetEnvironmentVariableA("AKUMA_RELAY", env, sizeof(env))) lstrcpynA(relay_pick, env, sizeof(relay_pick));
    if (GetEnvironmentVariableA("AKUMA_NET", env, sizeof(env))) lstrcpynA(g_net, env, sizeof(g_net));
    if (GetEnvironmentVariableA("AKUMA_NETPASS", env, sizeof(env))) lstrcpynA(g_pass, env, sizeof(g_pass));
    if (GetEnvironmentVariableA("AKUMA_NICK", env, sizeof(env))) lstrcpynA(g_nick, env, sizeof(g_nick));
    if (!relay_pick[0]) return;
    if (!strchr(relay_pick, '.') && !strchr(relay_pick, ':') && read_text("relays.txt", text, sizeof(text))) {
        // nome do cadastro
        for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
            char *l = trim(line);
            if (*l == '#' || !*l) continue;
            char *name = strtok(l, " \t");
            char *region = strtok(NULL, " \t");
            char *addr = strtok(NULL, " \t");
            (void)region;
            if (name && addr && !lstrcmpiA(name, relay_pick)) { set_host_port(addr); break; }
        }
    } else {
        set_host_port(relay_pick);
    }
    if (!g_net[0]) lstrcpyA(g_net, "akuma");
    g_relay_on = g_relay_host[0] != 0;
    if (GetEnvironmentVariableA("AKUMA_RELAY_OFF", env, sizeof(env)) && env[0] == '1') g_relay_on = 0;
}

// ---------------------------------------------------------------------------
// Tabela de sockets do jogo (modo relay)

typedef struct {
    int used;
    SOCKET s;
    int type;                  // SOCK_STREAM / SOCK_DGRAM
    uint16_t vport, rport;     // porta virtual vista pelo jogo; porta real em 127.0.0.1
    uint32_t bind_ip;
    int bound, listening, connected;
    uint32_t peer_ip;          // ordem de rede
    uint16_t peer_port;        // ordem de host
} Sock;

enum { MAX_SOCKS = 512, MAX_PEERMAP = 128, MAX_UDPSRC = 64, MAX_STREAMS = 128, MAX_PROXIES = 32 };
static Sock g_socks[MAX_SOCKS];
static uint16_t g_next_eph = 49152;

typedef struct { int used; uint16_t rport; uint32_t ip; uint16_t vport; } PeerMap;
static PeerMap g_peermap[MAX_PEERMAP];
static int g_peermap_next;

typedef struct { int used; SOCKET s; uint16_t rport; uint32_t ip; uint16_t vport; } UdpSrc;
static UdpSrc g_udpsrc[MAX_UDPSRC];

typedef struct { int used; uint32_t ip; uint16_t port; SOCKET l; uint16_t rport; } Proxy;
static Proxy g_px[MAX_PROXIES];

static Sock *st_find(SOCKET s)
{
    for (int i = 0; i < MAX_SOCKS; ++i) if (g_socks[i].used && g_socks[i].s == s) return &g_socks[i];
    return NULL;
}

static Sock *st_add(SOCKET s, int type)
{
    for (int i = 0; i < MAX_SOCKS; ++i) {
        if (g_socks[i].used && g_socks[i].s == s) { memset(&g_socks[i], 0, sizeof(Sock)); g_socks[i].used = 1; g_socks[i].s = s; g_socks[i].type = type; return &g_socks[i]; }
    }
    for (int i = 0; i < MAX_SOCKS; ++i) {
        if (!g_socks[i].used) { memset(&g_socks[i], 0, sizeof(Sock)); g_socks[i].used = 1; g_socks[i].s = s; g_socks[i].type = type; return &g_socks[i]; }
    }
    return NULL;
}

static uint16_t alloc_eph(void)
{
    uint16_t p = g_next_eph++;
    if (g_next_eph < 49152) g_next_eph = 49152;
    return p;
}

// ---------------------------------------------------------------------------
// Relay: quadros

enum { C_HELLO = 1, C_UDP = 2, C_CONNECT = 3, T_DATA = 4, T_CLOSE = 5, C_PING = 6 };
enum { S_WELCOME = 0x81, S_UDP = 0x82, S_INCOMING = 0x83, S_PONG = 0x86, S_ERROR = 0x8F };
enum { MAX_FRAME = 16384, DATA_CHUNK = 8000 };

static SOCKET g_rl = INVALID_SOCKET;
static SOCKET g_wake = INVALID_SOCKET;
static uint16_t g_wake_port;

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

static int relay_send_frame(uint8_t type, const void *a, int alen, const void *b, int blen)
{
    static uint8_t buf[3 + MAX_FRAME];
    if (1 + alen + blen > MAX_FRAME - 1) return 0;
    int ok = 0;
    EnterCriticalSection(&g_sendcs);
    if (g_rl != INVALID_SOCKET) {
        wr16(buf, (uint16_t)(1 + alen + blen));
        buf[2] = type;
        if (alen) memcpy(buf + 3, a, (size_t)alen);
        if (blen) memcpy(buf + 3 + alen, b, (size_t)blen);
        int total = 3 + alen + blen, off = 0;
        ok = 1;
        while (off < total) {
            int n = send(g_rl, (const char *)buf + off, total - off, 0);
            if (n <= 0) { ok = 0; break; }
            off += n;
        }
    }
    LeaveCriticalSection(&g_sendcs);
    return ok;
}

static void write_status(const char *text)
{
    char path[MAX_PATH];
    exe_path("relay-status.txt", path, sizeof(path));
    HANDLE h = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD w;
    WriteFile(h, text, (DWORD)lstrlenA(text), &w, NULL);
    CloseHandle(h);
}

// ---------------------------------------------------------------------------
// Relay: thread

typedef struct { int used; SOCKET s; uint32_t ivip; uint32_t sid; int initiator; } Stream;
static Stream g_str[MAX_STREAMS];
static uint32_t g_next_sid = 1;

static void wake_thread(void)
{
    if (g_wake == INVALID_SOCKET) return;
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(g_wake_port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sendto(g_wake, "w", 1, 0, (struct sockaddr *)&a, sizeof(a));
}

static void stream_close(Stream *s, int tell)
{
    if (!s->used) return;
    if (tell) {
        uint8_t h[9];
        memcpy(h, &s->ivip, 4);
        wr32(h + 4, s->sid);
        h[8] = (uint8_t)(s->initiator ? 0 : 1);
        relay_send_frame(T_CLOSE, h, 9, NULL, 0);
    }
    closesocket(s->s);
    s->used = 0;
}

static Stream *stream_find(uint32_t ivip, uint32_t sid, int initiator)
{
    for (int i = 0; i < MAX_STREAMS; ++i)
        if (g_str[i].used && g_str[i].ivip == ivip && g_str[i].sid == sid && g_str[i].initiator == initiator) return &g_str[i];
    return NULL;
}

static Stream *stream_new(SOCKET s, uint32_t ivip, uint32_t sid, int initiator)
{
    for (int i = 0; i < MAX_STREAMS; ++i) {
        if (!g_str[i].used) {
            g_str[i].used = 1; g_str[i].s = s; g_str[i].ivip = ivip; g_str[i].sid = sid; g_str[i].initiator = initiator;
            return &g_str[i];
        }
    }
    return NULL;
}

static void tune(SOCKET s)
{
    int one = 1, buf = 256 * 1024, timeout = 3000;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof(one));
    setsockopt(s, SOL_SOCKET, SO_SNDBUF, (const char *)&buf, sizeof(buf));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout, sizeof(timeout));
}

static struct sockaddr_in loop_addr(uint16_t port)
{
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return a;
}

static uint16_t local_port(SOCKET s)
{
    struct sockaddr_in a;
    int l = sizeof(a);
    if (getsockname(s, (struct sockaddr *)&a, &l)) return 0;
    return ntohs(a.sin_port);
}

static void peermap_add(uint16_t rport, uint32_t ip, uint16_t vport)
{
    EnterCriticalSection(&g_cs);
    PeerMap *m = &g_peermap[g_peermap_next++ % MAX_PEERMAP];
    m->used = 1; m->rport = rport; m->ip = ip; m->vport = vport;
    LeaveCriticalSection(&g_cs);
}

// Entrada de um datagrama UDP vindo do relay: injeta no(s) socket(s) reais do jogo.
// Porta 47624 (descoberta): no Windows quem escuta ali e o servidor de nomes do DirectPlay, que
// repassa a pergunta ao socket UDP do jogo anfitriao (2350 em diante). Aqui o shim faz esse papel.
static void inject_udp(uint32_t srcip, uint16_t srcport, uint16_t dstport, const uint8_t *data, int len)
{
    uint16_t targets[8];
    int nt = 0;
    SOCKET inj = INVALID_SOCKET;
    EnterCriticalSection(&g_cs);
    for (int i = 0; i < MAX_SOCKS && nt < 8; ++i)
        if (g_socks[i].used && g_socks[i].type == SOCK_DGRAM && g_socks[i].rport &&
            (g_socks[i].vport == dstport || (dstport == 47624 && g_socks[i].vport >= 2350 && g_socks[i].vport < 2400)))
            targets[nt++] = g_socks[i].rport;
    if (nt) {
        for (int i = 0; i < MAX_UDPSRC; ++i)
            if (g_udpsrc[i].used && g_udpsrc[i].ip == srcip && g_udpsrc[i].vport == srcport) { inj = g_udpsrc[i].s; break; }
        if (inj == INVALID_SOCKET) {
            for (int i = 0; i < MAX_UDPSRC; ++i) {
                if (g_udpsrc[i].used) continue;
                SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
                struct sockaddr_in a = loop_addr(0);
                if (s == INVALID_SOCKET || bind(s, (struct sockaddr *)&a, sizeof(a))) { if (s != INVALID_SOCKET) closesocket(s); break; }
                g_udpsrc[i].used = 1; g_udpsrc[i].s = s; g_udpsrc[i].rport = local_port(s); g_udpsrc[i].ip = srcip; g_udpsrc[i].vport = srcport;
                inj = s;
                break;
            }
        }
    }
    LeaveCriticalSection(&g_cs);
    if (inj == INVALID_SOCKET) { wlog("relay: UDP para a porta %u sem socket local (descartado)", dstport); return; }
    for (int i = 0; i < nt; ++i) {
        struct sockaddr_in a = loop_addr(targets[i]);
        sendto(inj, (const char *)data, len, 0, (struct sockaddr *)&a, sizeof(a));
    }
}

static void on_incoming(uint32_t ivip, uint32_t sid, uint16_t srcport, uint16_t dstport)
{
    uint16_t lport = 0;
    EnterCriticalSection(&g_cs);
    for (int i = 0; i < MAX_SOCKS; ++i)
        if (g_socks[i].used && g_socks[i].type == SOCK_STREAM && g_socks[i].listening && g_socks[i].vport == dstport) { lport = g_socks[i].rport; break; }
    LeaveCriticalSection(&g_cs);
    uint8_t h[9];
    memcpy(h, &ivip, 4); wr32(h + 4, sid); h[8] = 1;
    SOCKET c = INVALID_SOCKET;
    Stream *st = NULL;
    if (lport) {
        c = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in b = loop_addr(0), t = loop_addr(lport);
        if (c != INVALID_SOCKET && !bind(c, (struct sockaddr *)&b, sizeof(b))) {
            peermap_add(local_port(c), ivip, srcport);
            tune(c);
            if (!connect(c, (struct sockaddr *)&t, sizeof(t))) st = stream_new(c, ivip, sid, 0);
        }
    }
    if (!st) {
        wlog("relay: TCP de %u.%u.%u.%u para a porta %u sem listener local", ((uint8_t *)&ivip)[0], ((uint8_t *)&ivip)[1], ((uint8_t *)&ivip)[2], ((uint8_t *)&ivip)[3], dstport);
        if (c != INVALID_SOCKET) closesocket(c);
        relay_send_frame(T_CLOSE, h, 9, NULL, 0);
    } else {
        wlog("relay: TCP aceito de %u.%u.%u.%u:%u para a porta %u (sid %lu)", ((uint8_t *)&ivip)[0], ((uint8_t *)&ivip)[1], ((uint8_t *)&ivip)[2], ((uint8_t *)&ivip)[3], srcport, dstport, sid);
    }
}

static int send_all(SOCKET s, const uint8_t *p, int n)
{
    int off = 0;
    while (off < n) {
        int k = send(s, (const char *)p + off, n - off, 0);
        if (k <= 0) return 0;
        off += k;
    }
    return 1;
}

static void on_frame(uint8_t type, const uint8_t *p, int n)
{
    switch (type) {
    case S_UDP:
        if (n >= 8) {
            uint32_t ip; memcpy(&ip, p, 4);
            inject_udp(ip, rd16(p + 4), rd16(p + 6), p + 8, n - 8);
        }
        break;
    case S_INCOMING:
        if (n >= 12) { uint32_t ip; memcpy(&ip, p, 4); on_incoming(ip, rd32(p + 4), rd16(p + 8), rd16(p + 10)); }
        break;
    case T_DATA:
    case T_CLOSE:
        if (n >= 9) {
            uint32_t ip; memcpy(&ip, p, 4);
            // dir 0 = vem do iniciador, entao aqui eu sou o destino (initiator = 0).
            Stream *s = stream_find(ip, rd32(p + 4), p[8] == 0 ? 0 : 1);
            if (!s) break;
            if (type == T_CLOSE) { stream_close(s, 0); break; }
            if (!send_all(s->s, p + 9, n - 9)) { wlog("relay: falha ao entregar dados ao jogo, fechando fluxo"); stream_close(s, 1); }
        }
        break;
    case S_ERROR:
        wlog("relay: ERRO %d: %.*s", n ? p[0] : 0, n > 1 ? n - 1 : 0, p + 1);
        break;
    default:
        break;
    }
}

static int recv_exact(SOCKET s, uint8_t *p, int n)
{
    int off = 0;
    while (off < n) {
        int k = recv(s, (char *)p + off, n - off, 0);
        if (k <= 0) return 0;
        off += k;
    }
    return 1;
}

static SOCKET relay_connect(char *err, int errcap)
{
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)g_relay_port);
    unsigned long ip = inet_addr(g_relay_host);
    if (ip == INADDR_NONE) {
        struct hostent *h = gethostbyname(g_relay_host);
        if (!h || !h->h_addr_list[0]) { lstrcpynA(err, "nome do relay nao resolve", errcap); return INVALID_SOCKET; }
        memcpy(&ip, h->h_addr_list[0], 4);
    }
    a.sin_addr.s_addr = ip;
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) { lstrcpynA(err, "socket falhou", errcap); return s; }
    int one = 1, to = 6000;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof(one));
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&to, sizeof(to));
    if (connect(s, (struct sockaddr *)&a, sizeof(a))) {
        wsprintfA(err, "nao conectou ao relay (erro %d)", WSAGetLastError());
        closesocket(s);
        return INVALID_SOCKET;
    }
    // HELLO
    uint8_t h[3 + 40 * 3 + 2];
    int k = 0;
    h[k++] = 1;
    int nl = lstrlenA(g_net), pl = lstrlenA(g_pass), kl = lstrlenA(g_nick);
    h[k++] = (uint8_t)nl; memcpy(h + k, g_net, (size_t)nl); k += nl;
    h[k++] = (uint8_t)pl; memcpy(h + k, g_pass, (size_t)pl); k += pl;
    h[k++] = (uint8_t)kl; memcpy(h + k, g_nick, (size_t)kl); k += kl;
    EnterCriticalSection(&g_sendcs);
    g_rl = s;
    LeaveCriticalSection(&g_sendcs);
    if (!relay_send_frame(C_HELLO, h, k, NULL, 0)) { lstrcpynA(err, "falha ao enviar HELLO", errcap); goto fail; }
    for (;;) {
        uint8_t hd[2];
        if (!recv_exact(s, hd, 2)) { lstrcpynA(err, "relay nao respondeu", errcap); goto fail; }
        int len = rd16(hd);
        static uint8_t body[MAX_FRAME];
        if (len < 1 || len >= MAX_FRAME || !recv_exact(s, body, len)) { lstrcpynA(err, "resposta invalida do relay", errcap); goto fail; }
        if (body[0] == S_WELCOME && len >= 7) {
            memcpy((void *)&g_vip, body + 1, 4);
            to = 0;
            setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&to, sizeof(to));
            return s;
        }
        if (body[0] == S_ERROR) {
            wsprintfA(err, "relay recusou: %.*s", len > 2 ? len - 2 : 0, (char *)body + 2);
            goto fail;
        }
    }
fail:
    EnterCriticalSection(&g_sendcs);
    g_rl = INVALID_SOCKET;
    LeaveCriticalSection(&g_sendcs);
    closesocket(s);
    return INVALID_SOCKET;
}

static void drop_all_streams(void)
{
    for (int i = 0; i < MAX_STREAMS; ++i) if (g_str[i].used) stream_close(&g_str[i], 0);
}

static DWORD WINAPI relay_thread(LPVOID unused)
{
    (void)unused;
    static uint8_t in[2 * MAX_FRAME];
    for (;;) {
        char err[160] = "";
        SOCKET rs = relay_connect(err, sizeof(err));
        if (rs == INVALID_SOCKET) {
            wlog("relay: %s", err);
            char st[256];
            wsprintfA(st, "erro: %s\r\n", err);
            write_status(st);
            Sleep(3000);
            continue;
        }
        uint32_t vip = g_vip;
        const uint8_t *v = (const uint8_t *)&vip;
        char st[256];
        wsprintfA(st, "conectado: %s:%d rede=%s ip=%u.%u.%u.%u\r\n", g_relay_host, g_relay_port, g_net, v[0], v[1], v[2], v[3]);
        write_status(st);
        wlog("relay: conectado, IP virtual %u.%u.%u.%u", v[0], v[1], v[2], v[3]);
        SetEvent(g_vip_event);
        int in_len = 0;
        DWORD last_ping = GetTickCount();
        for (;;) {
            fd_set rd;
            FD_ZERO(&rd);
            FD_SET(rs, &rd);
            FD_SET(g_wake, &rd);
            EnterCriticalSection(&g_cs);
            for (int i = 0; i < MAX_PROXIES; ++i) if (g_px[i].used) FD_SET(g_px[i].l, &rd);
            LeaveCriticalSection(&g_cs);
            for (int i = 0; i < MAX_STREAMS; ++i) if (g_str[i].used) FD_SET(g_str[i].s, &rd);
            struct timeval tv = {5, 0};
            int r = select(0, &rd, NULL, NULL, &tv);
            if (r < 0) { Sleep(5); }
            DWORD now = GetTickCount();
            if (now - last_ping > 15000) {
                uint8_t tok[4];
                wr32(tok, now);
                relay_send_frame(C_PING, tok, 4, NULL, 0);
                last_ping = now;
            }
            if (r <= 0) continue;
            if (FD_ISSET(g_wake, &rd)) { char junk[64]; recv(g_wake, junk, sizeof(junk), 0); }
            if (FD_ISSET(rs, &rd)) {
                int k = recv(rs, (char *)in + in_len, (int)sizeof(in) - in_len, 0);
                if (k <= 0) { wlog("relay: conexao perdida (%d)", WSAGetLastError()); break; }
                in_len += k;
                int off = 0;
                while (in_len - off >= 2) {
                    int len = rd16(in + off);
                    if (len < 1 || len >= MAX_FRAME) { wlog("relay: quadro invalido"); in_len = -1; break; }
                    if (in_len - off < 2 + len) break;
                    on_frame(in[off + 2], in + off + 3, len - 1);
                    off += 2 + len;
                }
                if (in_len < 0) break;
                if (off) { memmove(in, in + off, (size_t)(in_len - off)); in_len -= off; }
            }
            // proxies: o jogo conectou a um IP virtual
            for (int i = 0; i < MAX_PROXIES; ++i) {
                SOCKET l = INVALID_SOCKET;
                uint32_t dip = 0;
                uint16_t dport = 0;
                EnterCriticalSection(&g_cs);
                if (g_px[i].used) { l = g_px[i].l; dip = g_px[i].ip; dport = g_px[i].port; }
                LeaveCriticalSection(&g_cs);
                if (l == INVALID_SOCKET || !FD_ISSET(l, &rd)) continue;
                struct sockaddr_in pa;
                int pl = sizeof(pa);
                SOCKET c = accept(l, (struct sockaddr *)&pa, &pl);
                if (c == INVALID_SOCKET) continue;
                tune(c);
                uint16_t srcport = 0;
                EnterCriticalSection(&g_cs);
                for (int j = 0; j < MAX_SOCKS; ++j)
                    if (g_socks[j].used && g_socks[j].type == SOCK_STREAM && g_socks[j].rport == ntohs(pa.sin_port)) { srcport = g_socks[j].vport; break; }
                LeaveCriticalSection(&g_cs);
                if (!srcport) srcport = alloc_eph();
                uint32_t sid = g_next_sid++;
                Stream *s = stream_new(c, g_vip, sid, 1);
                if (!s) { closesocket(c); continue; }
                uint8_t w[12];
                wr32(w, sid);
                memcpy(w + 4, &dip, 4);
                wr16(w + 8, dport);
                wr16(w + 10, srcport);
                relay_send_frame(C_CONNECT, w, 12, NULL, 0);
                wlog("relay: jogo abriu TCP para %u.%u.%u.%u:%u (sid %lu)", ((uint8_t *)&dip)[0], ((uint8_t *)&dip)[1], ((uint8_t *)&dip)[2], ((uint8_t *)&dip)[3], dport, sid);
            }
            // dados dos sockets do jogo
            for (int i = 0; i < MAX_STREAMS; ++i) {
                Stream *s = &g_str[i];
                if (!s->used || !FD_ISSET(s->s, &rd)) continue;
                uint8_t buf[9 + DATA_CHUNK];
                int k = recv(s->s, (char *)buf + 9, DATA_CHUNK, 0);
                if (k <= 0) { stream_close(s, 1); continue; }
                memcpy(buf, &s->ivip, 4);
                wr32(buf + 4, s->sid);
                buf[8] = (uint8_t)(s->initiator ? 0 : 1);
                if (!relay_send_frame(T_DATA, buf, 9 + k, NULL, 0)) { wlog("relay: falha ao enviar"); in_len = -1; break; }
            }
            if (in_len < 0) break;
        }
        EnterCriticalSection(&g_sendcs);
        g_rl = INVALID_SOCKET;
        LeaveCriticalSection(&g_sendcs);
        closesocket(rs);
        drop_all_streams();
        write_status("erro: conexao com o relay perdida, reconectando\r\n");
        Sleep(2000);
    }
}

static void relay_start(void)
{
    if (!g_relay_on || InterlockedExchange(&g_started, 1)) return;
    WSADATA wd;
    WSAStartup(MAKEWORD(2, 2), &wd);
    g_vip_event = CreateEventA(NULL, TRUE, FALSE, NULL);
    g_wake = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in a = loop_addr(0);
    bind(g_wake, (struct sockaddr *)&a, sizeof(a));
    g_wake_port = local_port(g_wake);
    HANDLE t = CreateThread(NULL, 0, relay_thread, NULL, 0, NULL);
    if (t) CloseHandle(t);
    wlog("relay: thread iniciada, relay %s:%d rede '%s'", g_relay_host, g_relay_port, g_net);
}

static int relay_wait_vip(DWORD ms)
{
    if (g_vip) return 1;
    if (!g_vip_event) return 0;
    WaitForSingleObject(g_vip_event, ms);
    return g_vip != 0;
}

static Proxy *proxy_get(uint32_t ip, uint16_t port)
{
    EnterCriticalSection(&g_cs);
    Proxy *p = NULL;
    for (int i = 0; i < MAX_PROXIES; ++i) if (g_px[i].used && g_px[i].ip == ip && g_px[i].port == port) { p = &g_px[i]; break; }
    if (!p) {
        for (int i = 0; i < MAX_PROXIES && !p; ++i) {
            if (g_px[i].used) continue;
            SOCKET l = socket(AF_INET, SOCK_STREAM, 0);
            struct sockaddr_in a = loop_addr(0);
            if (l == INVALID_SOCKET) break;
            if (bind(l, (struct sockaddr *)&a, sizeof(a)) || listen(l, 16)) { closesocket(l); break; }
            g_px[i].used = 1; g_px[i].ip = ip; g_px[i].port = port; g_px[i].l = l; g_px[i].rport = local_port(l);
            p = &g_px[i];
        }
        if (p) wake_thread();
    }
    LeaveCriticalSection(&g_cs);
    return p;
}

// ---------------------------------------------------------------------------
// Ganchos. Cada um chama a funcao real e (opcionalmente) loga.

static int is_loopback(uint32_t ip) { return (ip & 0xFF) == 127; }

// ---------------------------------------------------------------------------
// IP real -> IP virtual.
// O provedor grava no cabecalho de cada mensagem o endereco REAL do remetente (IP da placa e
// porta, num formato embaralhado que ele mesmo decodifica no proprio buffer depois do recv) e
// responde para esse endereco. Na LAN virtual esse IP nao existe: aprendemos qual IP virtual
// enviou a mensagem e, quando o jogo tenta conectar/enviar para o IP real, trocamos pelo virtual.
typedef struct { DWORD tid; const unsigned char *buf; int len; int off; DWORD when; uint32_t vip; } RecvRec;
static RecvRec g_rr[16];
typedef struct { uint32_t real, vip; DWORD when; } RealMap;
static RealMap g_rm[32];
static uint32_t g_hint_vip;
static DWORD g_hint_when;

static int is_virtual(uint32_t ip) { return (ip & 0xFFFF) == 0x4D0A; } // 10.77.x.x em ordem de rede

static void note_recv(const char *buf, int len, int off, uint32_t vip)
{
    if (!g_relay_on || !is_virtual(vip) || len < off + 12) return;
    DWORD tid = GetCurrentThreadId();
    RecvRec *r = &g_rr[tid % 16];
    r->tid = tid; r->buf = (const unsigned char *)buf; r->len = len; r->off = off; r->when = GetTickCount(); r->vip = vip;
    g_hint_vip = vip; g_hint_when = r->when;
}

// Chamado no inicio dos ganchos: se o provedor ja decodificou o cabecalho do ultimo recv desta
// thread, aprende IP real -> IP virtual.
static void learn_peek(void)
{
    if (!g_relay_on) return;
    DWORD tid = GetCurrentThreadId();
    RecvRec *r = &g_rr[tid % 16];
    if (r->tid != tid || !r->buf || GetTickCount() - r->when > 3000) return;
    const unsigned char *p = r->buf + r->off;
    unsigned port = (unsigned)(p[2] << 8 | p[3]);
    uint32_t ip;
    if (p[0] != 2 || p[1] != 0 || port < 2300 || port >= 2400) return;
    memcpy(&ip, p + 4, 4);
    if (!ip || is_virtual(ip) || (ip & 0xFF) == 127) return;
    EnterCriticalSection(&g_cs);
    int slot = -1, oldest = 0;
    for (int i = 0; i < 32; ++i) {
        if (g_rm[i].real == ip) { slot = i; break; }
        if (g_rm[i].when < g_rm[oldest].when) oldest = i;
    }
    if (slot < 0) slot = oldest;
    if (g_rm[slot].real != ip || g_rm[slot].vip != r->vip)
        wlog("aprendido: IP real %u.%u.%u.%u -> virtual %u.%u.%u.%u", ((uint8_t *)&ip)[0], ((uint8_t *)&ip)[1], ((uint8_t *)&ip)[2], ((uint8_t *)&ip)[3],
             ((uint8_t *)&r->vip)[0], ((uint8_t *)&r->vip)[1], ((uint8_t *)&r->vip)[2], ((uint8_t *)&r->vip)[3]);
    g_rm[slot].real = ip; g_rm[slot].vip = r->vip; g_rm[slot].when = GetTickCount();
    LeaveCriticalSection(&g_cs);
    r->buf = NULL;
}

// Destino que o jogo usou -> destino na LAN virtual (0 = nenhum).
static uint32_t resolve_dest(uint32_t ip)
{
    if (is_virtual(ip) || (ip & 0xFF) == 127 || ip == 0 || ip == 0xFFFFFFFF || (ip >> 24) == 255) return 0;
    uint32_t v = 0;
    EnterCriticalSection(&g_cs);
    for (int i = 0; i < 32; ++i) if (g_rm[i].real == ip && g_rm[i].vip) { v = g_rm[i].vip; break; }
    if (!v && g_hint_vip && GetTickCount() - g_hint_when < 5000) v = g_hint_vip;
    LeaveCriticalSection(&g_cs);
    return v;
}

static int implicit_bind(Sock *st)
{
    if (st->rport) return 1;
    struct sockaddr_in a = loop_addr(0);
    if (bind(st->s, (struct sockaddr *)&a, sizeof(a))) return 0;
    st->rport = local_port(st->s);
    if (!st->vport) st->vport = alloc_eph();
    st->bound = 1;
    return 1;
}

static int WSAAPI hk_WSAStartup(WORD ver, LPWSADATA data)
{
    int r = WSAStartup(ver, data);
    wlog("WSAStartup(0x%04x) = %d", ver, r);
    if (r == 0 && g_relay_on) relay_start();
    return r;
}

static int WSAAPI hk_WSACleanup(void)
{
    int r = WSACleanup();
    int e = WSAGetLastError();
    wlog("WSACleanup() = %d", r);
    WSASetLastError(e);
    return r;
}

static SOCKET WSAAPI hk_socket(int af, int type, int proto)
{
    SOCKET s = socket(af, type, proto);
    int e = WSAGetLastError();
    wlog("socket(af=%d,type=%d,proto=%d) = %u err=%d", af, type, proto, (unsigned)s, s == INVALID_SOCKET ? e : 0);
    if (s != INVALID_SOCKET && g_relay_on) {
        EnterCriticalSection(&g_cs);
        st_add(s, type);
        LeaveCriticalSection(&g_cs);
    }
    WSASetLastError(e);
    return s;
}

static int WSAAPI hk_bind(SOCKET s, const struct sockaddr *name, int len)
{
    char b[48];
    int r, e;
    if (g_relay_on && name && name->sa_family == AF_INET && len >= (int)sizeof(struct sockaddr_in)) {
        const struct sockaddr_in *in = (const struct sockaddr_in *)name;
        struct sockaddr_in a = loop_addr(0);
        r = bind(s, (struct sockaddr *)&a, sizeof(a));
        e = WSAGetLastError();
        if (r == 0) {
            EnterCriticalSection(&g_cs);
            Sock *st = st_find(s);
            if (st) {
                st->rport = local_port(s);
                st->vport = ntohs(in->sin_port) ? ntohs(in->sin_port) : alloc_eph();
                st->bind_ip = in->sin_addr.s_addr;
                st->bound = 1;
            }
            LeaveCriticalSection(&g_cs);
        }
        wlog("bind(%u, %s) = %d err=%d [relay: real 127.0.0.1:%u]", (unsigned)s, addrs(name, len, b), r, r ? e : 0, r ? 0 : (unsigned)local_port(s));
    } else {
        r = bind(s, name, len);
        e = WSAGetLastError();
        wlog("bind(%u, %s) = %d err=%d", (unsigned)s, addrs(name, len, b), r, r ? e : 0);
    }
    WSASetLastError(e);
    return r;
}

static int WSAAPI hk_listen(SOCKET s, int backlog)
{
    int r = listen(s, backlog);
    int e = WSAGetLastError();
    wlog("listen(%u, %d) = %d err=%d", (unsigned)s, backlog, r, r ? e : 0);
    if (g_relay_on && r == 0) {
        EnterCriticalSection(&g_cs);
        Sock *st = st_find(s);
        if (st) st->listening = 1;
        LeaveCriticalSection(&g_cs);
    }
    WSASetLastError(e);
    return r;
}

static SOCKET WSAAPI hk_accept(SOCKET s, struct sockaddr *addr, int *alen)
{
    struct sockaddr_in real;
    int rl = sizeof(real);
    SOCKET n = accept(s, g_relay_on ? (struct sockaddr *)&real : addr, g_relay_on ? &rl : alen);
    int e = WSAGetLastError();
    char b[48] = "-";
    if (n != INVALID_SOCKET && g_relay_on) {
        uint32_t pip = 0;
        uint16_t pport = 0;
        EnterCriticalSection(&g_cs);
        for (int i = 0; i < MAX_PEERMAP; ++i)
            if (g_peermap[i].used && g_peermap[i].rport == ntohs(real.sin_port)) { pip = g_peermap[i].ip; pport = g_peermap[i].vport; }
        Sock *ls = st_find(s);
        Sock *ns = st_add(n, SOCK_STREAM);
        if (ns) {
            ns->bound = 1; ns->connected = 1; ns->rport = local_port(n); ns->vport = ls ? ls->vport : 0;
            ns->peer_ip = pip; ns->peer_port = pport;
        }
        LeaveCriticalSection(&g_cs);
        struct sockaddr_in v;
        memset(&v, 0, sizeof(v));
        v.sin_family = AF_INET; v.sin_addr.s_addr = pip; v.sin_port = htons(pport);
        if (addr && alen && *alen >= (int)sizeof(v)) { memcpy(addr, &v, sizeof(v)); *alen = sizeof(v); }
        addrs((struct sockaddr *)&v, sizeof(v), b);
    } else if (n != INVALID_SOCKET && addr && alen) {
        addrs(addr, *alen, b);
    }
    wlog("accept(%u) = %u peer=%s err=%d", (unsigned)s, (unsigned)n, b, n == INVALID_SOCKET && e != WSAEWOULDBLOCK ? e : 0);
    WSASetLastError(e);
    return n;
}

static struct { DWORD tid; const char *buf; int len; DWORD when; } g_lastrecv[16];

static int WSAAPI hk_connect(SOCKET s, const struct sockaddr *name, int len)
{
    char b[48];
    int r, e;
    struct sockaddr_in remap;
    const struct sockaddr_in *in = (const struct sockaddr_in *)name;
    int remapped = 0;
    learn_peek();
    if (g_relay_on && name && name->sa_family == AF_INET && len >= (int)sizeof(*in)) {
        uint32_t v = resolve_dest(in->sin_addr.s_addr);
        if (v) {
            remap = *in; remap.sin_addr.s_addr = v; in = &remap; remapped = 1;
            // O cabecalho de uma maquina diferente traz porta lixo (ex. 37576): o listener
            // do provedor fica em 2300-2349. Na LAN virtual cada cliente tem seu proprio 2300.
            unsigned p = ntohs(remap.sin_port);
            if (p < 2300 || p >= 2350) remap.sin_port = htons(2300);
            wlog("  remap: %s -> %u.%u.%u.%u", addrs(name, len, b), ((uint8_t *)&v)[0], ((uint8_t *)&v)[1], ((uint8_t *)&v)[2], ((uint8_t *)&v)[3]);
        }
    }
    int redirect = g_relay_on && name && name->sa_family == AF_INET && len >= (int)sizeof(*in) && !is_loopback(in->sin_addr.s_addr);
    if (redirect) {
        EnterCriticalSection(&g_cs);
        Sock *st = st_find(s);
        if (st && st->type == SOCK_STREAM) {
            if (!st->vport) st->vport = alloc_eph();
            st->peer_ip = in->sin_addr.s_addr;
            st->peer_port = ntohs(in->sin_port);
        }
        LeaveCriticalSection(&g_cs);
        Proxy *p = proxy_get(in->sin_addr.s_addr, ntohs(in->sin_port));
        if (!p) { r = SOCKET_ERROR; e = WSAENETUNREACH; }
        else {
            struct sockaddr_in a = loop_addr(p->rport);
            r = connect(s, (struct sockaddr *)&a, sizeof(a));
            e = WSAGetLastError();
            EnterCriticalSection(&g_cs);
            st = st_find(s);
            if (st && st->type == SOCK_STREAM) {
                st->rport = local_port(s);
                if (r == 0 || e == WSAEWOULDBLOCK) st->connected = 1;
            }
            LeaveCriticalSection(&g_cs);
        }
    } else {
        r = connect(s, name, len);
        e = WSAGetLastError();
    }
    {
        // Diagnostico: o buffer do ultimo recvfrom desta thread, agora (o provedor pode ter gravado
        // nele o endereco de resposta).
        int k = (int)(GetCurrentThreadId() % 16);
        if (g_lastrecv[k].tid == GetCurrentThreadId() && GetTickCount() - g_lastrecv[k].when < 3000) {
            char h2[330];
            wlog("  (buffer do ultimo recvfrom, agora) %s", hexs(g_lastrecv[k].buf, g_lastrecv[k].len < 64 ? g_lastrecv[k].len : 64, h2));
        }
    }
    wlog("connect(%u, %s) = %d err=%d%s", (unsigned)s, addrs(name, len, b), r, r ? e : 0, redirect ? (remapped ? " [relay, remapeado]" : " [relay]") : "");
    WSASetLastError(e);
    return r;
}

static int WSAAPI hk_send(SOCKET s, const char *buf, int len, int flags)
{
    int r = send(s, buf, len, flags);
    int e = WSAGetLastError();
    char h[330];
    wlog("send(%u, len=%d, flags=%d) = %d err=%d  %s", (unsigned)s, len, flags, r, r < 0 ? e : 0, hexs(buf, len, h));
    WSASetLastError(e);
    return r;
}

static int WSAAPI hk_recv(SOCKET s, char *buf, int len, int flags)
{
    learn_peek();
    int r = recv(s, buf, len, flags);
    if (r > 0 && g_relay_on) {
        uint32_t pv = 0;
        EnterCriticalSection(&g_cs);
        Sock *st = st_find(s);
        if (st) pv = st->peer_ip;
        LeaveCriticalSection(&g_cs);
        if (pv) note_recv(buf, r, 0, pv);
    }
    int e = WSAGetLastError();
    if (!(r < 0 && e == WSAEWOULDBLOCK)) {
        char h[330];
        wlog("recv(%u, len=%d, flags=%d) = %d err=%d  %s", (unsigned)s, len, flags, r, r < 0 ? e : 0, r > 0 ? hexs(buf, r, h) : "");
    }
    WSASetLastError(e);
    return r;
}

static int WSAAPI hk_sendto(SOCKET s, const char *buf, int len, int flags, const struct sockaddr *to, int tolen)
{
    char b[48], h[330];
    int r, e;
    struct sockaddr_in remap;
    const struct sockaddr_in *in = (const struct sockaddr_in *)to;
    learn_peek();
    if (g_relay_on && to && to->sa_family == AF_INET && tolen >= (int)sizeof(*in)) {
        uint32_t v = resolve_dest(in->sin_addr.s_addr);
        if (v) {
            remap = *in; remap.sin_addr.s_addr = v; in = &remap;
            unsigned p = ntohs(remap.sin_port);
            if (p != 47624 && (p < 2350 || p >= 2400)) remap.sin_port = htons(2350);
        }
    }
    int redirect = g_relay_on && to && to->sa_family == AF_INET && tolen >= (int)sizeof(*in) && !is_loopback(in->sin_addr.s_addr);
    if (redirect) {
        EnterCriticalSection(&g_cs);
        Sock *st = st_find(s);
        int ok = st && st->type == SOCK_DGRAM && implicit_bind(st);
        uint16_t vp = ok ? st->vport : 0;
        LeaveCriticalSection(&g_cs);
        if (!ok) { r = SOCKET_ERROR; e = WSAENOTSOCK; }
        else if (len > MAX_FRAME - 16) { r = SOCKET_ERROR; e = WSAEMSGSIZE; }
        else {
            uint8_t head[8];
            wr16(head, vp);
            memcpy(head + 2, &in->sin_addr.s_addr, 4);
            wr16(head + 6, ntohs(in->sin_port));
            if (relay_send_frame(C_UDP, head, 8, buf, len)) { r = len; e = 0; }
            else { r = SOCKET_ERROR; e = WSAENETDOWN; }
        }
    } else {
        r = sendto(s, buf, len, flags, to, tolen);
        e = WSAGetLastError();
    }
    wlog("sendto(%u, %s, len=%d) = %d err=%d%s  %s", (unsigned)s, addrs(to, tolen, b), len, r, r < 0 ? e : 0, redirect ? " [relay]" : "", hexs(buf, len, h));
    WSASetLastError(e);
    return r;
}


static int WSAAPI hk_recvfrom(SOCKET s, char *buf, int len, int flags, struct sockaddr *from, int *fromlen)
{
    learn_peek();
    int r = recvfrom(s, buf, len, flags, from, fromlen);
    int e = WSAGetLastError();
    char b[48] = "-", h[330];
    if (r > 0) {
        DWORD tid = GetCurrentThreadId();
        int k = (int)(tid % 16);
        g_lastrecv[k].tid = tid; g_lastrecv[k].buf = buf; g_lastrecv[k].len = r; g_lastrecv[k].when = GetTickCount();
    }
    if (r >= 0 && g_relay_on && from && fromlen && *fromlen >= (int)sizeof(struct sockaddr_in)) {
        struct sockaddr_in *in = (struct sockaddr_in *)from;
        EnterCriticalSection(&g_cs);
        for (int i = 0; i < MAX_UDPSRC; ++i)
            if (g_udpsrc[i].used && g_udpsrc[i].rport == ntohs(in->sin_port)) {
                in->sin_addr.s_addr = g_udpsrc[i].ip;
                in->sin_port = htons(g_udpsrc[i].vport);
                break;
            }
        LeaveCriticalSection(&g_cs);
    }
    if (r > 0 && g_relay_on && from && fromlen && *fromlen >= (int)sizeof(struct sockaddr_in))
        note_recv(buf, r, 4, ((struct sockaddr_in *)from)->sin_addr.s_addr);
    if (r >= 0 && from && fromlen) addrs(from, *fromlen, b);
    if (!(r < 0 && e == WSAEWOULDBLOCK))
        wlog("recvfrom(%u, len=%d) = %d from=%s err=%d  %s", (unsigned)s, len, r, b, r < 0 ? e : 0, r > 0 ? hexs(buf, r, h) : "");
    WSASetLastError(e);
    return r;
}

static void fdlist(const fd_set *f, char *out, int cap)
{
    out[0] = 0;
    if (!f) return;
    for (u_int i = 0; i < f->fd_count && lstrlenA(out) < cap - 12; ++i) wsprintfA(out + lstrlenA(out), "%s%u", i ? "," : "", (unsigned)f->fd_array[i]);
}

static int WSAAPI hk_select(int n, fd_set *rd, fd_set *wr, fd_set *ex, const struct timeval *tv)
{
    // Chamado em loop de espera; so loga quando algo esta pronto ou ha erro.
    int in_r = rd ? (int)rd->fd_count : 0, in_w = wr ? (int)wr->fd_count : 0;
    int r = select(n, rd, wr, ex, tv);
    int e = WSAGetLastError();
    if (r != 0) {
        char a[100], b[100], c[100];
        fdlist(rd, a, sizeof(a)); fdlist(wr, b, sizeof(b)); fdlist(ex, c, sizeof(c));
        wlog("select(in r=%d w=%d, timeout=%ld.%06lds) = %d prontos r=[%s] w=[%s] e=[%s] err=%d", in_r, in_w, tv ? tv->tv_sec : -1, tv ? tv->tv_usec : 0, r, a, b, c, r < 0 ? e : 0);
    }
    WSASetLastError(e);
    return r;
}

static int WSAAPI hk_setsockopt(SOCKET s, int level, int name, const char *val, int len)
{
    int r = setsockopt(s, level, name, val, len);
    int e = WSAGetLastError();
    char h[330];
    wlog("setsockopt(%u, level=%d, opt=%d, len=%d) = %d err=%d  %s", (unsigned)s, level, name, len, r, r ? e : 0, hexs(val, len, h));
    WSASetLastError(e);
    return r;
}

static int WSAAPI hk_getsockopt(SOCKET s, int level, int name, char *val, int *len)
{
    int r = getsockopt(s, level, name, val, len);
    int e = WSAGetLastError();
    char h[330];
    wlog("getsockopt(%u, level=%d, opt=%d) = %d err=%d  %s", (unsigned)s, level, name, r, r ? e : 0, r == 0 && len ? hexs(val, *len, h) : "");
    WSASetLastError(e);
    return r;
}

static int WSAAPI hk_getsockname(SOCKET s, struct sockaddr *name, int *len)
{
    int r = getsockname(s, name, len);
    int e = WSAGetLastError();
    char b[48] = "-", real[48] = "-";
    if (r == 0 && name && len) addrs(name, *len, real);
    if (r == 0 && g_relay_on && name && len && *len >= (int)sizeof(struct sockaddr_in)) {
        struct sockaddr_in *in = (struct sockaddr_in *)name;
        EnterCriticalSection(&g_cs);
        Sock *st = st_find(s);
        if (st && st->bound) {
            in->sin_port = htons(st->vport);
            in->sin_addr.s_addr = st->connected ? g_vip : st->bind_ip;
        }
        LeaveCriticalSection(&g_cs);
    }
    if (r == 0 && name && len) addrs(name, *len, b);
    wlog("getsockname(%u) = %d %s%s%s err=%d", (unsigned)s, r, b, g_relay_on ? " real=" : "", g_relay_on ? real : "", r ? e : 0);
    WSASetLastError(e);
    return r;
}

static int WSAAPI hk_getpeername(SOCKET s, struct sockaddr *name, int *len)
{
    int r = getpeername(s, name, len);
    int e = WSAGetLastError();
    char b[48] = "-";
    if (r == 0 && g_relay_on && name && len && *len >= (int)sizeof(struct sockaddr_in)) {
        struct sockaddr_in *in = (struct sockaddr_in *)name;
        EnterCriticalSection(&g_cs);
        Sock *st = st_find(s);
        if (st && st->peer_ip) {
            in->sin_addr.s_addr = st->peer_ip;
            in->sin_port = htons(st->peer_port);
        }
        LeaveCriticalSection(&g_cs);
    }
    if (r == 0 && name && len) addrs(name, *len, b);
    wlog("getpeername(%u) = %d %s err=%d", (unsigned)s, r, b, r ? e : 0);
    WSASetLastError(e);
    return r;
}

static int WSAAPI hk_ioctlsocket(SOCKET s, long cmd, u_long *arg)
{
    u_long before = arg ? *arg : 0;
    int r = ioctlsocket(s, cmd, arg);
    int e = WSAGetLastError();
    wlog("ioctlsocket(%u, cmd=0x%08lx, arg=%lu) = %d err=%d out=%lu", (unsigned)s, (unsigned long)cmd, before, r, r ? e : 0, arg ? *arg : 0);
    WSASetLastError(e);
    return r;
}

static int WSAAPI hk_closesocket(SOCKET s)
{
    if (g_relay_on) {
        EnterCriticalSection(&g_cs);
        Sock *st = st_find(s);
        if (st) st->used = 0;
        LeaveCriticalSection(&g_cs);
    }
    int r = closesocket(s);
    int e = WSAGetLastError();
    wlog("closesocket(%u) = %d err=%d", (unsigned)s, r, r ? e : 0);
    WSASetLastError(e);
    return r;
}

static struct hostent g_he[8];
static char *g_he_addr[8][2];
static char *g_he_alias[8][1];
static uint32_t g_he_ip[8];
static char g_he_name[8][64];
static int g_he_next;

static struct hostent *virtual_host(const char *name)
{
    EnterCriticalSection(&g_cs);
    int i = g_he_next++ & 7;
    g_he_ip[i] = g_vip;
    g_he_addr[i][0] = (char *)&g_he_ip[i];
    g_he_addr[i][1] = NULL;
    g_he_alias[i][0] = NULL;
    lstrcpynA(g_he_name[i], name && *name ? name : "akuma", sizeof(g_he_name[i]));
    g_he[i].h_name = g_he_name[i];
    g_he[i].h_aliases = g_he_alias[i];
    g_he[i].h_addrtype = AF_INET;
    g_he[i].h_length = 4;
    g_he[i].h_addr_list = g_he_addr[i];
    LeaveCriticalSection(&g_cs);
    return &g_he[i];
}

static struct hostent *WSAAPI hk_gethostbyname(const char *name)
{
    char local[128] = "";
    struct hostent *r = NULL;
    int virt = 0;
    if (g_relay_on) {
        gethostname(local, sizeof(local));
        int is_local = !name || !*name || !lstrcmpiA(name, local);
        if (is_local && relay_wait_vip(8000)) { r = virtual_host(name && *name ? name : local); virt = 1; }
    }
    if (!r) r = gethostbyname(name);
    int e = WSAGetLastError();
    char ip[24] = "-";
    if (r && r->h_addr_list && r->h_addr_list[0]) {
        const unsigned char *b = (const unsigned char *)r->h_addr_list[0];
        wsprintfA(ip, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    }
    wlog("gethostbyname(\"%s\") = %s%s err=%d", name ? name : "(null)", ip, virt ? " [relay: IP virtual]" : "", r ? 0 : e);
    WSASetLastError(e);
    return r;
}

static int WSAAPI hk_gethostname(char *name, int len)
{
    int r = gethostname(name, len);
    int e = WSAGetLastError();
    wlog("gethostname() = %d \"%s\"", r, r == 0 ? name : "");
    WSASetLastError(e);
    return r;
}

static unsigned long WSAAPI hk_inet_addr(const char *s)
{
    unsigned long r = inet_addr(s);
    wlog("inet_addr(\"%s\") = 0x%08lx", s ? s : "(null)", r);
    return r;
}

static char *WSAAPI hk_inet_ntoa(struct in_addr a)
{
    char *r = inet_ntoa(a);
    wlog("inet_ntoa(0x%08lx) = \"%s\"", (unsigned long)a.s_addr, r ? r : "");
    return r;
}

// ---------------------------------------------------------------------------
// Troca da IAT do dpwsockx.dll

typedef struct { const char *name; void *fn; void *real; } HookEntry;
static HookEntry g_hooks[] = {
    {"WSAStartup", (void *)hk_WSAStartup, NULL}, {"WSACleanup", (void *)hk_WSACleanup, NULL},
    {"socket", (void *)hk_socket, NULL}, {"bind", (void *)hk_bind, NULL}, {"listen", (void *)hk_listen, NULL},
    {"accept", (void *)hk_accept, NULL}, {"connect", (void *)hk_connect, NULL}, {"send", (void *)hk_send, NULL},
    {"recv", (void *)hk_recv, NULL}, {"sendto", (void *)hk_sendto, NULL}, {"recvfrom", (void *)hk_recvfrom, NULL},
    {"select", (void *)hk_select, NULL}, {"setsockopt", (void *)hk_setsockopt, NULL},
    {"getsockopt", (void *)hk_getsockopt, NULL}, {"getsockname", (void *)hk_getsockname, NULL},
    {"getpeername", (void *)hk_getpeername, NULL}, {"ioctlsocket", (void *)hk_ioctlsocket, NULL},
    {"closesocket", (void *)hk_closesocket, NULL}, {"gethostbyname", (void *)hk_gethostbyname, NULL},
    {"gethostname", (void *)hk_gethostname, NULL}, {"inet_addr", (void *)hk_inet_addr, NULL},
    {"inet_ntoa", (void *)hk_inet_ntoa, NULL},
};
enum { HOOK_COUNT = sizeof(g_hooks) / sizeof(g_hooks[0]) };

static int patch_module(HMODULE mod)
{
    BYTE *base = (BYTE *)mod;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return 0;
    int patched = 0, pending = 0;
    HMODULE ws = GetModuleHandleA("ws2_32.dll");
    for (int i = 0; i < HOOK_COUNT; ++i) if (!g_hooks[i].real && ws) g_hooks[i].real = (void *)GetProcAddress(ws, g_hooks[i].name);
    for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir.VirtualAddress); d->Name; ++d) {
        const char *dll = (const char *)(base + d->Name);
        wlog("import: %s (OFT=%lx FT=%lx)", dll, (unsigned long)d->OriginalFirstThunk, (unsigned long)d->FirstThunk);
        if (lstrcmpiA(dll, "ws2_32.dll") && lstrcmpiA(dll, "wsock32.dll")) continue;
        IMAGE_THUNK_DATA *oft = (IMAGE_THUNK_DATA *)(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        IMAGE_THUNK_DATA *ft = (IMAGE_THUNK_DATA *)(base + d->FirstThunk);
        // O dpwsockx importa por ORDINAL; casa pelo endereco ja resolvido na IAT.
        for (; oft->u1.AddressOfData; ++oft, ++ft) {
            int i;
            for (i = 0; i < HOOK_COUNT; ++i) {
                if ((void *)ft->u1.Function == g_hooks[i].fn) break;
                if (g_hooks[i].real && (void *)ft->u1.Function == g_hooks[i].real) {
                    DWORD old;
                    if (VirtualProtect(&ft->u1.Function, sizeof(void *), PAGE_READWRITE, &old)) {
                        ft->u1.Function = (DWORD_PTR)g_hooks[i].fn;
                        VirtualProtect(&ft->u1.Function, sizeof(void *), old, &old);
                        ++patched;
                        wlog("  hook %s", g_hooks[i].name);
                    }
                    break;
                }
            }
            if (i == HOOK_COUNT) ++pending;
        }
    }
    wlog("IAT do dpwsockx: %d funcoes trocadas, %d nao ganchadas (ordinais sem gancho) (modulo %p)", patched, pending, (void *)mod);
    return patched;
}

typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;
typedef struct { ULONG Flags; const USTR *FullDllName; const USTR *BaseDllName; PVOID DllBase; ULONG SizeOfImage; } LDR_LOADED;
typedef VOID(NTAPI *LDR_NOTIFY)(ULONG reason, const LDR_LOADED *data, PVOID ctx);
typedef LONG(NTAPI *LDR_REGISTER)(ULONG flags, LDR_NOTIFY fn, PVOID ctx, PVOID *cookie);

static VOID NTAPI dll_notify(ULONG reason, const LDR_LOADED *d, PVOID ctx)
{
    (void)ctx;
    if (reason != 1 || !d || !d->BaseDllName) return;   // 1 = LDR_DLL_NOTIFICATION_REASON_LOADED
    static const WCHAR want[] = L"dpwsockx.dll";
    if (d->BaseDllName->Length / 2 == 12 && !_wcsnicmp(d->BaseDllName->Buffer, want, 12)) {
        wlog("dpwsockx.dll carregada em %p (%lu bytes)", d->DllBase, d->SizeOfImage);
        patch_module((HMODULE)d->DllBase);
    }
}

static void netshim_init(void)
{
    char dir[MAX_PATH];
    GetModuleFileNameA(NULL, dir, sizeof(dir));
    char *slash = strrchr(dir, '\\');
    if (slash) slash[1] = 0;
    lstrcpynA(g_dir, dir, sizeof(g_dir));
    InitializeCriticalSection(&g_logcs);
    InitializeCriticalSection(&g_cs);
    InitializeCriticalSection(&g_sendcs);
    g_t0 = GetTickCount();
    char flag[2] = "";
    char path[MAX_PATH];
    exe_path("winsocklog.enable", path, sizeof(path));
    int log = (GetEnvironmentVariableA("AKUMA_WINSOCKLOG", flag, sizeof(flag)) == 1 && flag[0] == '1') ||
              GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
    load_config();
    if (log) {
        exe_path("winsocklog.txt", path, sizeof(path));
        g_log = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (g_log == INVALID_HANDLE_VALUE) {
            char alt[48];
            wsprintfA(alt, "winsocklog-%lu.txt", GetCurrentProcessId());
            exe_path(alt, path, sizeof(path));
            g_log = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        }
        g_log_on = g_log != INVALID_HANDLE_VALUE;
    }
    if (!g_log_on && !g_relay_on) return;   // nada a fazer: o jogo usa o Winsock direto
    wlog("NetShim pid=%lu log=%d relay=%d %s:%d rede='%s'", GetCurrentProcessId(), g_log_on, g_relay_on, g_relay_host, g_relay_port, g_net);
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    LDR_REGISTER reg = nt ? (LDR_REGISTER)GetProcAddress(nt, "LdrRegisterDllNotification") : NULL;
    PVOID cookie;
    if (reg) wlog("LdrRegisterDllNotification = %ld", reg(0, dll_notify, NULL, &cookie));
    else wlog("LdrRegisterDllNotification indisponivel");
    HMODULE already = GetModuleHandleA("dpwsockx.dll");
    if (already) patch_module(already);
}

// Chamado pelo DllMain da AkumaHud.dll (CdAudio.c), em DLL_PROCESS_ATTACH.
void NetShimAttach(void)
{
    netshim_init();
}

// ---------------------------------------------------------------------------
// Autoteste (compilado com -DSHIM_SELFTEST): um unico processo faz o papel de host e de
// visitante pelo relay (laco consigo mesmo), chamando os ganchos como o dpwsockx faria.
#ifdef SHIM_LOADTEST
int main(void)
{
    netshim_init();
    HMODULE m = LoadLibraryA("dpwsockx.dll");
    wlog("LoadLibrary = %p", (void *)m);
    return 0;
}
#endif
#ifdef SHIM_SELFTEST
#include <stdio.h>
#define CHECK(c) do { if (!(c)) { printf("FALHOU linha %d: %s\n", __LINE__, #c); return 1; } } while (0)
int main(void)
{
    g_log_on = 1;
    netshim_init();
    CHECK(g_relay_on);
    WSADATA wd;
    CHECK(hk_WSAStartup(MAKEWORD(2, 2), &wd) == 0);
    struct hostent *h = hk_gethostbyname("");
    CHECK(h && h->h_addr_list[0]);
    uint32_t me; memcpy(&me, h->h_addr_list[0], 4);
    printf("meu IP virtual: %u.%u.%u.%u\n", ((uint8_t *)&me)[0], ((uint8_t *)&me)[1], ((uint8_t *)&me)[2], ((uint8_t *)&me)[3]);

    // host: UDP 47624 (descoberta) e TCP 2300
    SOCKET du = hk_socket(AF_INET, SOCK_DGRAM, 0), ls = hk_socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a; memset(&a, 0, sizeof(a)); a.sin_family = AF_INET;
    a.sin_port = htons(47624);
    CHECK(hk_bind(du, (struct sockaddr *)&a, sizeof(a)) == 0);
    a.sin_port = htons(2300);
    CHECK(hk_bind(ls, (struct sockaddr *)&a, sizeof(a)) == 0);
    CHECK(hk_listen(ls, 5) == 0);
    int al = sizeof(a);
    CHECK(hk_getsockname(ls, (struct sockaddr *)&a, &al) == 0 && ntohs(a.sin_port) == 2300);

    // visitante: broadcast da descoberta
    SOCKET cu = hk_socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in bc; memset(&bc, 0, sizeof(bc)); bc.sin_family = AF_INET; bc.sin_port = htons(47624); bc.sin_addr.s_addr = INADDR_BROADCAST;
    CHECK(hk_sendto(cu, "ENUM", 4, 0, (struct sockaddr *)&bc, sizeof(bc)) == 4);
    Sleep(300);
    char buf[64]; struct sockaddr_in from; int fl = sizeof(from);
    int n = hk_recvfrom(du, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fl);
    CHECK(n == 4 && !memcmp(buf, "ENUM", 4));
    CHECK(from.sin_addr.s_addr == me);
    printf("broadcast recebido de %u.%u.%u.%u:%u\n", ((uint8_t *)&from.sin_addr)[0], ((uint8_t *)&from.sin_addr)[1], ((uint8_t *)&from.sin_addr)[2], ((uint8_t *)&from.sin_addr)[3], ntohs(from.sin_port));
    // resposta unicast
    CHECK(hk_sendto(du, "REPLY", 5, 0, (struct sockaddr *)&from, fl) == 5);
    Sleep(300);
    n = hk_recvfrom(cu, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fl);
    CHECK(n == 5 && !memcmp(buf, "REPLY", 5) && ntohs(from.sin_port) == 47624);

    // TCP: visitante conecta ao IP virtual do host
    SOCKET cs = hk_socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in dst; memset(&dst, 0, sizeof(dst)); dst.sin_family = AF_INET; dst.sin_port = htons(2300); dst.sin_addr.s_addr = me;
    CHECK(hk_connect(cs, (struct sockaddr *)&dst, sizeof(dst)) == 0);
    Sleep(400);
    struct sockaddr_in peer; int pl = sizeof(peer);
    SOCKET as = hk_accept(ls, (struct sockaddr *)&peer, &pl);
    CHECK(as != INVALID_SOCKET);
    CHECK(peer.sin_addr.s_addr == me);
    CHECK(hk_send(cs, "hello", 5, 0) == 5);
    Sleep(300);
    n = hk_recv(as, buf, sizeof(buf), 0);
    CHECK(n == 5 && !memcmp(buf, "hello", 5));
    CHECK(hk_send(as, "world!", 6, 0) == 6);
    Sleep(300);
    n = hk_recv(cs, buf, sizeof(buf), 0);
    CHECK(n == 6 && !memcmp(buf, "world!", 6));
    // 200 KB num sentido, para exercitar fragmentacao
    static char big[200000], got[200000];
    for (int i = 0; i < (int)sizeof(big); ++i) big[i] = (char)(i * 7);
    int sent = 0, recvd = 0;
    u_long nb = 1;
    ioctlsocket(as, FIONBIO, &nb);
    DWORD t0 = GetTickCount();
    while (recvd < (int)sizeof(big) && GetTickCount() - t0 < 5000) {
        if (sent < (int)sizeof(big)) { int k = hk_send(cs, big + sent, sizeof(big) - sent > 5000 ? 5000 : (int)sizeof(big) - sent, 0); if (k > 0) sent += k; }
        int k = recv(as, got + recvd, sizeof(got) - recvd, 0);
        if (k > 0) recvd += k; else Sleep(1);
    }
    CHECK(recvd == (int)sizeof(big) && !memcmp(big, got, sizeof(big)));
    printf("200 KB ok\n");
    hk_closesocket(cs);
    Sleep(300);
    nb = 0;
    n = recv(as, buf, sizeof(buf), 0);
    CHECK(n == 0);
    printf("fechamento propagado\nOK\n");
    return 0;
}
#endif
