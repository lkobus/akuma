# Relay do Akuma: resumo para uma nova conversa

Objetivo: jogar o Multi Play do Akuma pela internet sem VPN e sem abrir portas, com uma
"LAN virtual" onde todos os jogadores se enxergam. **Sem mexer na lógica do jogo.**

Contexto completo do netcode: [netcode.md](netcode.md). Este arquivo é só o necessário.

## Requisitos (do dono do projeto)

1. Um relay por região (Brasil, Ásia…). Cada um é um **software em C com uma única
   responsabilidade: resolver o acesso online**.
2. Um **cadastro de relays**, para o jogador escolher/colar o IP do relay no cliente.
3. Pode ser uma máquina bem barata na Ásia. Teste inicial num servidor Windows no Brasil, então
   **binários Windows e Linux** a partir do mesmo código.
4. Não alterar o jogo: em vez de conectar por LAN, o cliente conecta no relay.
5. Todos os jogadores da mesma "rede" se enxergam (descoberta de sala incluída).

## O que o jogo faz (resumo)

- DirectPlay 3A (`IDirectPlay3A`) com o provedor **TCP/IP** do Windows. Sessão criada com
  `DPSESSION_MIGRATEHOST | KEEPALIVE`, sem `DPSESSION_DIRECTPLAYPROTOCOL`.
- Topologia: um host de sessão + clientes. Toda mensagem é `Send` garantido, em broadcast.
- Host decide mobs; cada cliente é a fonte do próprio herói. Migração de host funciona.
- Tick de 25 Hz. Mensagens pequenas (5 tipos; `0x60` por mapa e por tick, quase todas de 8 a 24
  bytes; o snapshot de entrada tem ~9 KB).

## Fato novo que define o desenho

`dpwsockx.dll` (o provedor TCP/IP do DirectPlay) importa só **26 funções BSD simples** de
`ws2_32`: `socket bind listen accept connect send recv sendto recvfrom select setsockopt
getsockopt getsockname getpeername ioctlsocket closesocket gethostbyname gethostname inet_addr
inet_ntoa htons ntohs WSAStartup WSACleanup WSAGetLastError __WSAFDIsSet`. Não usa IOCP, nem
`WSAAsync`, nem `WSAEventSelect`. Isso torna viável **interceptar o Winsock dentro do processo do
jogo** (sem driver e sem admin) e levar o tráfego ao relay.

## Proposta: LAN virtual em duas peças

1. **Relay (servidor, C, Linux e Windows).** Um processo, um laço de eventos (`poll`/`select`,
   sem dependências). Responsabilidades: aceitar clientes, agrupá-los numa *rede* (nome + senha),
   atribuir um IP virtual a cada um (por exemplo `10.77.<rede>.<id>`), repassar fluxos TCP entre
   clientes e **emular o broadcast UDP** (a descoberta de salas) para todos da rede. Nada de
   lógica de jogo.
2. **Shim no cliente (C, dentro da `AkumaHud.dll` ou DLL própria).** Gancho nas 26 funções
   acima, só quando o relay está configurado: conexões para IPs `10.77.x.y` viram fluxos no
   relay, o broadcast de descoberta vai ao relay, e `bind`/`listen` do host ficam atrás do relay.
   O jogo continua achando que está numa LAN.

Cadastro de relays: arquivo de texto (`relays.txt`: nome, região, host:porta) lido pelo launcher,
com ping medido para sugerir o mais próximo.

## Plano em fases

0. **Descobrir o fluxo real**: log do Winsock do jogo (mesma ideia do `netlog`, com flag) em
   duas instâncias locais. Confirma portas (as portas conhecidas do DirectPlay, 47624 e a faixa
   2300–2400, são de memória e precisam ser confirmadas), o formato da descoberta e quando o
   provedor abre UDP vs TCP.
1. Relay mínimo que faz eco/loopback e shim funcionando com **duas instâncias no mesmo PC**.
2. Relay num servidor **Windows no Brasil** com jogadores reais.
3. Mesmo código compilado para Linux (o Zig em `.work/zig` já cruza Windows e Linux).
4. Segurança: TLS no canal cliente–relay, senha/convite por rede, limites de jogadores e de taxa.
5. Vários relays e o cadastro.

## Segurança (o que dá e o que não dá)

O jogo confia no cliente (HP e atributos vêm de cada máquina, o save é local). O relay não muda
isso. Ele pode: cifrar o canal (TLS), exigir senha/convite, limitar quem entra e a taxa de
mensagens, e recusar pacotes malformados. Não consegue provar que um save é legítimo.

## Hospedagem barata na Ásia (de memória, verificar preços e regiões)

O relay é leve (dezenas de MB de RAM, tráfego pequeno). Opções comuns: Oracle Cloud Always Free
(a região é escolhida no cadastro; há Singapura, Tóquio, Seul e Mumbai), AWS Lightsail
(Singapura/Tóquio), Vultr, Linode ou DigitalOcean (Singapura) e Hetzner (Singapura), na ordem de
US$ 4–6 por mês. Antes de decidir, medir a latência real dos jogadores até cada cidade.

## Ambiente de desenvolvimento

- Build do cliente: `.\dev-build.ps1 -NetLog -Kit -Second` instala em `dist\jogo\Akuma-Portatil` e
  `dist\jogo-2\Akuma-Portatil` (duas instâncias no mesmo PC; `jogo-2` tem saves e log próprios).
- Logs: `netlog.txt` em cada pasta (flag `netlog.enable`). Arquivar em `dist\logs\`.
- **Forma de trabalho combinada**: não dirigir a interface do jogo. Pôr logs sob flag, dizer o que
  rodar, ler os arquivos. O jogador deixa o jogo aberto se precisar de tempo real.
- Compilador C: Zig (`.work/zig/zig-x86_64-windows-0.14.1/zig.exe`, `zig cc -target ...`).
- Ferramentas de engenharia reversa: REA (Ghidra) para o `Akuma.exe` original em `.work\orig`.
  Varredura com capstone precisa de `skipdata=True`.

## Perguntas em aberto

- O provedor TCP/IP do DirectPlay chama `bind` em portas fixas ou faixa? E a descoberta usa
  broadcast de verdade ou também uma conexão TCP direta? (fase 0 responde.)
- Onde interceptar: `dpwsockx.dll` carrega depois do jogo; é preciso ganchar na carga da DLL (IAT
  do `dpwsockx`) ou com um gancho de `LoadLibrary`.
- Um relay por região significa redes separadas por região, ou o jogador escolhe uma rede em
  qualquer relay? (cada relay é uma "grande LAN" própria; unir regiões fica para depois.)
- Política de identidade: só nome de rede + senha, ou conta? O mínimo é senha de rede.
