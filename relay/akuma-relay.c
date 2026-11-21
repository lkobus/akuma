// akuma-relay: LAN virtual para o Akuma (DirectPlay) pela internet.
// Uma unica responsabilidade: acesso online. Sem logica de jogo.
//
//   - clientes entram numa "rede" (nome + senha) e recebem um IP virtual 10.77.<rede>.<n>
//   - o relay repassa fluxos TCP entre clientes da mesma rede
//   - o relay emula o broadcast UDP (descoberta de salas) para toda a rede
//
// Sem dependencias. Windows (WSAPoll) e Linux (poll). Protocolo em relay/PROTOCOLO.md.
//
//   akuma-relay [-p porta] [-m max_clientes] [-n max_por_rede] [-v]

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define SOCK_BAD INVALID_SOCKET
#define sock_close closesocket
#define sock_errno() WSAGetLastError()
#define E_WOULDBLOCK WSAEWOULDBLOCK
#define MSG_NOSIG 0
static void set_nonblock(sock_t s) { u_long on = 1; ioctlsocket(s, FIONBIO, &on); }
#define do_poll(f, n, t) WSAPoll((f), (ULONG)(n), (t))
#else
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
typedef int sock_t;
#define SOCK_BAD (-1)
#define sock_close close
#define sock_errno() errno
#define E_WOULDBLOCK EWOULDBLOCK
#define MSG_NOSIG MSG_NOSIGNAL
static void set_nonblock(sock_t s) { fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK); }
#define do_poll(f, n, t) poll((f), (nfds_t)(n), (t))
#endif

enum {
    VERSION = 1,
    MAX_FRAME = 16384,             // bytes depois do campo de tamanho
    IN_CAP = 2 + MAX_FRAME,
    OUT_CAP = 2 * 1024 * 1024,     // fila de saida por cliente; estourou, o cliente cai
    MAX_NETS = 64,
    MAX_STREAMS = 4096,
    IDLE_SECONDS = 60,
    RATE_BYTES_PER_SEC = 2 * 1024 * 1024
};

// Tipos de quadro. Cliente -> relay.
enum { C_HELLO = 1, C_UDP = 2, C_CONNECT = 3, T_DATA = 4, T_CLOSE = 5, C_PING = 6 };
// Relay -> cliente.
enum { S_WELCOME = 0x81, S_UDP = 0x82, S_INCOMING = 0x83, S_PONG = 0x86, S_ERROR = 0x8F };
// Codigos de S_ERROR.
enum { ERR_PROTOCOL = 1, ERR_PASSWORD = 2, ERR_NO_NETS = 3, ERR_FULL = 4, ERR_NAME = 5, ERR_RATE = 6 };

typedef struct {
    int used;
    char name[33];
    char pass[33];
    int members;
} Net;

typedef struct {
    sock_t fd;
    int used, hello, dead;
    uint8_t in[IN_CAP];
    int in_len;
    uint8_t *out;
    size_t out_len, out_cap;
    int net;
    uint8_t vip[4];
    char nick[33];
    char peer[64];
    time_t last;
    time_t rate_sec;
    uint32_t rate_bytes;
} Client;

typedef struct {
    int used, net;
    uint8_t ivip[4];
    uint32_t sid;
    int a, b;                      // iniciador, destino
} Stream;

static Client *g_clients;
static int g_max_clients = 256;
static int g_max_per_net = 16;
static int g_verbose;
static Net g_nets[MAX_NETS];
static Stream g_streams[MAX_STREAMS];

static void logf_(const char *fmt, ...)
{
    char stamp[32];
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", tm);
    printf("%s ", stamp);
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

// ---- saida ------------------------------------------------------------------

static void queue_frame(int ci, uint8_t type, const void *a, int alen, const void *b, int blen)
{
    Client *c = &g_clients[ci];
    if (!c->used || c->dead) return;
    size_t need = 2 + 1 + (size_t)alen + (size_t)blen;
    if (c->out_len + need > OUT_CAP) { c->dead = 1; logf_("[%s] fila de saida cheia, derrubando", c->peer); return; }
    if (c->out_len + need > c->out_cap) {
        size_t cap = c->out_cap ? c->out_cap : 4096;
        while (cap < c->out_len + need) cap *= 2;
        uint8_t *n = (uint8_t *)realloc(c->out, cap);
        if (!n) { c->dead = 1; return; }
        c->out = n;
        c->out_cap = cap;
    }
    uint8_t *w = c->out + c->out_len;
    wr16(w, (uint16_t)(1 + alen + blen));
    w[2] = type;
    if (alen) memcpy(w + 3, a, (size_t)alen);
    if (blen) memcpy(w + 3 + alen, b, (size_t)blen);
    c->out_len += need;
}

static void send_error(int ci, uint8_t code, const char *text)
{
    uint8_t head = code;
    queue_frame(ci, S_ERROR, &head, 1, text, (int)strlen(text));
}

static void flush_client(int ci)
{
    Client *c = &g_clients[ci];
    size_t off = 0;
    while (off < c->out_len) {
        int n = (int)send(c->fd, (const char *)c->out + off, (int)(c->out_len - off), MSG_NOSIG);
        if (n > 0) { off += (size_t)n; continue; }
        if (n < 0 && sock_errno() == E_WOULDBLOCK) break;
        c->dead = 1;
        break;
    }
    if (off) {
        memmove(c->out, c->out + off, c->out_len - off);
        c->out_len -= off;
    }
}

// ---- redes ------------------------------------------------------------------

static int name_ok(const uint8_t *s, int n)
{
    if (n < 1 || n > 32) return 0;
    for (int i = 0; i < n; ++i) {
        uint8_t ch = s[i];
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              ch == '_' || ch == '-' || ch == '.' || ch == ' '))
            return 0;
    }
    return 1;
}

static void close_streams_of(int ci)
{
    for (int i = 0; i < MAX_STREAMS; ++i) {
        Stream *s = &g_streams[i];
        if (!s->used || (s->a != ci && s->b != ci)) continue;
        // Avisa a outra ponta. dir = 1 quando o aviso vai do destino para o iniciador.
        if (s->a == ci && s->b != ci) {
            uint8_t h[9]; memcpy(h, s->ivip, 4); wr32(h + 4, s->sid); h[8] = 0;
            queue_frame(s->b, T_CLOSE, h, 9, NULL, 0);
        } else if (s->b == ci && s->a != ci) {
            uint8_t h[9]; memcpy(h, s->ivip, 4); wr32(h + 4, s->sid); h[8] = 1;
            queue_frame(s->a, T_CLOSE, h, 9, NULL, 0);
        }
        s->used = 0;
    }
}

static void drop_client(int ci, const char *why)
{
    Client *c = &g_clients[ci];
    if (!c->used) return;
    logf_("[%s] saiu%s%s (%s)", c->peer, c->hello ? " da rede " : "", c->hello ? g_nets[c->net].name : "", why);
    close_streams_of(ci);
    if (c->hello) {
        Net *n = &g_nets[c->net];
        if (--n->members <= 0) { logf_("rede '%s' vazia, removida", n->name); memset(n, 0, sizeof(*n)); }
    }
    sock_close(c->fd);
    free(c->out);
    memset(c, 0, sizeof(*c));
    c->fd = SOCK_BAD;
}

static int find_by_vip(int net, const uint8_t *vip)
{
    for (int i = 0; i < g_max_clients; ++i) {
        Client *c = &g_clients[i];
        if (c->used && c->hello && !c->dead && c->net == net && memcmp(c->vip, vip, 4) == 0) return i;
    }
    return -1;
}

static void handle_hello(int ci, const uint8_t *p, int n)
{
    Client *c = &g_clients[ci];
    if (n < 4 || p[0] != VERSION) { send_error(ci, ERR_PROTOCOL, "versao do protocolo"); c->dead = 1; return; }
    int nl = p[1];
    if (2 + nl + 1 > n) { c->dead = 1; return; }
    const uint8_t *name = p + 2;
    int pl = p[2 + nl];
    if (2 + nl + 1 + pl + 1 > n) { c->dead = 1; return; }
    const uint8_t *pass = p + 3 + nl;
    int kl = p[3 + nl + pl];
    if (4 + nl + pl + kl > n) { c->dead = 1; return; }
    const uint8_t *nick = p + 4 + nl + pl;
    if (!name_ok(name, nl) || pl > 32 || kl > 32) { send_error(ci, ERR_NAME, "nome de rede invalido"); c->dead = 1; return; }

    int ni = -1;
    for (int i = 0; i < MAX_NETS; ++i)
        if (g_nets[i].used && strlen(g_nets[i].name) == (size_t)nl && memcmp(g_nets[i].name, name, (size_t)nl) == 0) { ni = i; break; }
    if (ni >= 0) {
        Net *net = &g_nets[ni];
        if (strlen(net->pass) != (size_t)pl || memcmp(net->pass, pass, (size_t)pl) != 0) {
            send_error(ci, ERR_PASSWORD, "senha incorreta");
            c->dead = 1;
            logf_("[%s] senha incorreta para a rede '%s'", c->peer, net->name);
            return;
        }
        if (net->members >= g_max_per_net) { send_error(ci, ERR_FULL, "rede cheia"); c->dead = 1; return; }
    } else {
        for (int i = 0; i < MAX_NETS; ++i) if (!g_nets[i].used) { ni = i; break; }
        if (ni < 0) { send_error(ci, ERR_NO_NETS, "relay sem espaco para novas redes"); c->dead = 1; return; }
        Net *net = &g_nets[ni];
        memset(net, 0, sizeof(*net));
        net->used = 1;
        memcpy(net->name, name, (size_t)nl);
        memcpy(net->pass, pass, (size_t)pl);
        logf_("rede '%s' criada (%s)", net->name, pl ? "com senha" : "sem senha");
    }
    int slot = 0;
    for (int s = 1; s <= 253 && !slot; ++s) {
        uint8_t v[4] = {10, 77, (uint8_t)ni, (uint8_t)s};
        if (find_by_vip(ni, v) < 0) slot = s;
    }
    if (!slot) { send_error(ci, ERR_FULL, "rede cheia"); c->dead = 1; return; }
    c->hello = 1;
    c->net = ni;
    c->vip[0] = 10; c->vip[1] = 77; c->vip[2] = (uint8_t)ni; c->vip[3] = (uint8_t)slot;
    memcpy(c->nick, nick, (size_t)kl);
    c->nick[kl] = 0;
    g_nets[ni].members++;
    uint8_t w[6];
    memcpy(w, c->vip, 4);
    wr16(w + 4, (uint16_t)g_nets[ni].members);
    queue_frame(ci, S_WELCOME, w, 6, NULL, 0);
    logf_("[%s] '%s' entrou na rede '%s' como %d.%d.%d.%d (%d na rede)", c->peer, c->nick, g_nets[ni].name,
          c->vip[0], c->vip[1], c->vip[2], c->vip[3], g_nets[ni].members);
}

static int is_broadcast(const uint8_t *ip)
{
    return ip[3] == 255; // 255.255.255.255 ou o broadcast da sub-rede virtual
}

static void handle_udp(int ci, const uint8_t *p, int n)
{
    Client *c = &g_clients[ci];
    if (n < 8) return;
    uint16_t srcport = rd16(p), dstport = rd16(p + 6);
    const uint8_t *dst = p + 2, *data = p + 8;
    int dlen = n - 8;
    uint8_t h[8];
    memcpy(h, c->vip, 4);
    wr16(h + 4, srcport);
    wr16(h + 6, dstport);
    if (g_verbose) logf_("[%s] UDP %u -> %d.%d.%d.%d:%u (%d bytes)", c->peer, srcport, dst[0], dst[1], dst[2], dst[3], dstport, dlen);
    if (is_broadcast(dst)) {
        // Como numa LAN, o broadcast tambem chega a quem enviou.
        for (int i = 0; i < g_max_clients; ++i) {
            Client *o = &g_clients[i];
            if (o->used && o->hello && !o->dead && o->net == c->net) queue_frame(i, S_UDP, h, 8, data, dlen);
        }
    } else {
        int t = find_by_vip(c->net, dst);
        if (t >= 0) queue_frame(t, S_UDP, h, 8, data, dlen);
    }
}

static Stream *find_stream(int net, const uint8_t *ivip, uint32_t sid)
{
    for (int i = 0; i < MAX_STREAMS; ++i) {
        Stream *s = &g_streams[i];
        if (s->used && s->net == net && s->sid == sid && memcmp(s->ivip, ivip, 4) == 0) return s;
    }
    return NULL;
}

static void handle_connect(int ci, const uint8_t *p, int n)
{
    Client *c = &g_clients[ci];
    if (n < 12) return;
    uint32_t sid = rd32(p);
    const uint8_t *dst = p + 4;
    uint16_t dstport = rd16(p + 8), srcport = rd16(p + 10);
    uint8_t hc[9];
    memcpy(hc, c->vip, 4); wr32(hc + 4, sid); hc[8] = 1;
    int t = find_by_vip(c->net, dst);
    Stream *slot = NULL;
    if (t >= 0 && !find_stream(c->net, c->vip, sid))
        for (int i = 0; i < MAX_STREAMS; ++i) if (!g_streams[i].used) { slot = &g_streams[i]; break; }
    if (!slot) {
        if (g_verbose || t < 0) logf_("[%s] conexao TCP para %d.%d.%d.%d:%u recusada (%s)", c->peer, dst[0], dst[1], dst[2], dst[3], dstport,
                                      t < 0 ? "ninguem com esse IP" : "sem espaco");
        queue_frame(ci, T_CLOSE, hc, 9, NULL, 0);
        return;
    }
    slot->used = 1; slot->net = c->net; memcpy(slot->ivip, c->vip, 4); slot->sid = sid; slot->a = ci; slot->b = t;
    uint8_t w[12];
    memcpy(w, c->vip, 4); wr32(w + 4, sid); wr16(w + 8, srcport); wr16(w + 10, dstport);
    queue_frame(t, S_INCOMING, w, 12, NULL, 0);
    if (g_verbose) logf_("[%s] TCP %d.%d.%d.%d:%u -> %d.%d.%d.%d:%u aberto", c->peer, c->vip[0], c->vip[1], c->vip[2], c->vip[3], srcport,
                         dst[0], dst[1], dst[2], dst[3], dstport);
}

static void handle_stream_frame(int ci, uint8_t type, const uint8_t *p, int n)
{
    Client *c = &g_clients[ci];
    if (n < 9) return;
    Stream *s = find_stream(c->net, p, rd32(p + 4));
    if (!s) return;
    uint8_t dir = p[8];
    // dir 0 = do iniciador para o destino; dir 1 = o contrario. Confere o remetente.
    int to;
    if (dir == 0 && s->a == ci) to = s->b;
    else if (dir == 1 && s->b == ci) to = s->a;
    else return;
    queue_frame(to, type, p, 9, p + 9, n - 9);
    if (type == T_CLOSE) s->used = 0;
}

static void handle_frame(int ci, uint8_t type, const uint8_t *p, int n)
{
    Client *c = &g_clients[ci];
    if (type == C_HELLO) {
        if (c->hello) { c->dead = 1; return; }
        handle_hello(ci, p, n);
        return;
    }
    if (!c->hello) { c->dead = 1; return; }
    switch (type) {
    case C_UDP: handle_udp(ci, p, n); break;
    case C_CONNECT: handle_connect(ci, p, n); break;
    case T_DATA:
    case T_CLOSE: handle_stream_frame(ci, type, p, n); break;
    case C_PING: if (n >= 4) queue_frame(ci, S_PONG, p, 4, NULL, 0); break;
    default: break;
    }
}

static void read_client(int ci)
{
    Client *c = &g_clients[ci];
    int n = (int)recv(c->fd, (char *)c->in + c->in_len, IN_CAP - c->in_len, 0);
    if (n == 0) { c->dead = 1; return; }
    if (n < 0) { if (sock_errno() != E_WOULDBLOCK) c->dead = 1; return; }
    c->in_len += n;
    time_t now = time(NULL);
    c->last = now;
    if (c->rate_sec != now) { c->rate_sec = now; c->rate_bytes = 0; }
    c->rate_bytes += (uint32_t)n;
    if (c->rate_bytes > RATE_BYTES_PER_SEC) { send_error(ci, ERR_RATE, "limite de taxa"); logf_("[%s] limite de taxa", c->peer); c->dead = 1; return; }
    int off = 0;
    while (!c->dead && c->in_len - off >= 2) {
        int len = rd16(c->in + off);
        if (len < 1 || len > MAX_FRAME - 1) { c->dead = 1; break; }
        if (c->in_len - off < 2 + len) break;
        handle_frame(ci, c->in[off + 2], c->in + off + 3, len - 1);
        off += 2 + len;
    }
    if (off) { memmove(c->in, c->in + off, (size_t)(c->in_len - off)); c->in_len -= off; }
}

static void accept_client(sock_t ls)
{
    struct sockaddr_in a;
    socklen_t al = sizeof(a);
    sock_t fd = accept(ls, (struct sockaddr *)&a, &al);
    if (fd == SOCK_BAD) return;
    int slot = -1;
    for (int i = 0; i < g_max_clients; ++i) if (!g_clients[i].used) { slot = i; break; }
    if (slot < 0) { sock_close(fd); logf_("recusado: relay cheio"); return; }
    set_nonblock(fd);
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof(one));
    Client *c = &g_clients[slot];
    memset(c, 0, sizeof(*c));
    c->fd = fd;
    c->used = 1;
    c->last = time(NULL);
    unsigned char *ip = (unsigned char *)&a.sin_addr;
    snprintf(c->peer, sizeof(c->peer), "%u.%u.%u.%u:%u", ip[0], ip[1], ip[2], ip[3], (unsigned)ntohs(a.sin_port));
}

int main(int argc, char **argv)
{
    int port = 47700;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-p") && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-m") && i + 1 < argc) g_max_clients = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-n") && i + 1 < argc) g_max_per_net = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-v")) g_verbose = 1;
        else { fprintf(stderr, "uso: %s [-p porta] [-m max_clientes] [-n max_por_rede] [-v]\n", argv[0]); return 2; }
    }
    if (g_max_clients < 1 || g_max_clients > 4096) g_max_clients = 256;
    if (g_max_per_net < 1 || g_max_per_net > 250) g_max_per_net = 16;
#ifdef _WIN32
    WSADATA wd;
    if (WSAStartup(MAKEWORD(2, 2), &wd)) { fprintf(stderr, "WSAStartup falhou\n"); return 1; }
#else
    signal(SIGPIPE, SIG_IGN);
#endif
    g_clients = (Client *)calloc((size_t)g_max_clients, sizeof(Client));
    struct pollfd *pf = (struct pollfd *)calloc((size_t)g_max_clients + 1, sizeof(*pf));
    int *pf_client = (int *)calloc((size_t)g_max_clients + 1, sizeof(int));
    if (!g_clients || !pf || !pf_client) return 1;
    for (int i = 0; i < g_max_clients; ++i) g_clients[i].fd = SOCK_BAD;

    sock_t ls = socket(AF_INET, SOCK_STREAM, 0);
#ifndef _WIN32
    int one = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof(one));
#endif
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(ls, (struct sockaddr *)&a, sizeof(a)) || listen(ls, 64)) {
        fprintf(stderr, "nao consegui escutar na porta TCP %d (erro %d)\n", port, sock_errno());
        return 1;
    }
    set_nonblock(ls);
    logf_("akuma-relay v%d na porta TCP %d (max %d clientes, %d por rede)", VERSION, port, g_max_clients, g_max_per_net);

    for (;;) {
        int n = 0;
        pf[n].fd = ls; pf[n].events = POLLIN; pf[n].revents = 0; pf_client[n] = -1; ++n;
        for (int i = 0; i < g_max_clients; ++i) {
            Client *c = &g_clients[i];
            if (!c->used) continue;
            pf[n].fd = c->fd;
            pf[n].events = (short)(POLLIN | (c->out_len ? POLLOUT : 0));
            pf[n].revents = 0;
            pf_client[n] = i;
            ++n;
        }
        int r = do_poll(pf, n, 1000);
        if (r < 0) {
#ifndef _WIN32
            if (errno == EINTR) continue;
#endif
            fprintf(stderr, "poll falhou (%d)\n", sock_errno());
            return 1;
        }
        if (pf[0].revents & POLLIN) accept_client(ls);
        for (int k = 1; k < n; ++k) {
            int ci = pf_client[k];
            Client *c = &g_clients[ci];
            if (!c->used) continue;
            if (pf[k].revents & (POLLERR | POLLHUP | POLLNVAL)) c->dead = 1;
            if (!c->dead && (pf[k].revents & POLLIN)) read_client(ci);
        }
        time_t now = time(NULL);
        for (int i = 0; i < g_max_clients; ++i) {
            Client *c = &g_clients[i];
            if (!c->used) continue;
            if (c->out_len && !c->dead) flush_client(i);
            if (!c->dead && now - c->last > IDLE_SECONDS) c->dead = 1;
            if (c->dead) {
                if (c->out_len) flush_client(i); // tenta entregar o ultimo erro
                drop_client(i, "desconectado");
            }
        }
    }
}
