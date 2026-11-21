# Netcode do Akuma (estudo)

Notas da engenharia reversa do `Akuma.exe` original (SHA-256 `11f8c6e0…a0a7`; os endereços são
VAs com base 0x400000). Tudo aqui foi observado no binário, exceto onde está marcado
como **hipótese**.

O Slayers (`E:\projetos\slayers\slayers-pro`) é um fork do Akuma e já extraiu histórico,
mapas e sprites (`docs/akuma`). O netcode é outra história: o Slayers tem servidor TCP próprio
(`Slayers.Revival.TCPServer`); o Akuma usa DirectPlay.

## Camada de transporte (DirectPlay)

| Fato | Evidência |
| --- | --- |
| **DirectPlay 3A via COM** | `DPLAYX.dll` importada; `CoCreateInstance` com `CLSID_DirectPlay` (`D1EB6D20-8923-11D0-9D97-00A0C90A43CB`) em `0x401ae0`; interface `IDirectPlay3A` (`133EFE41-…`) em `objeto+8` |
| Também usa **lobby** | `DirectPlayLobbyCreateA` em `0x401cf0`, guardada em `objeto+4` |
| Provedores reconhecidos | `DPSPGUID_TCPIP` (`0x489360`) e `DPSPGUID_MODEM` (`0x489380`) no handler do diálogo `0x40d410` |
| Não há winsock próprio | nenhum import de `wsock32`/`ws2_32`; tudo passa pelo DirectPlay |
| Classe de rede global | instância em `0x49B9A8`; `+4` lobby, `+8` IDirectPlay3A, `+0x20` buffer de envio, `+0x24` tamanho, `+0x28` HWND do diálogo |
| Wrappers finos | `0x401f80` CreatePlayer, `0x401fe0` **Send**, `0x402010` **Receive**, `0x4020c0` SetPlayerData, `0x4020f0` GetPlayerData |
| Persistência | local: `MULTISAV.001-009` (multi) e `LEGENSAV.001` (campanha) |

Globais de sessão: `0x4998d8` = DPID local, `0x4998dc` = **sou o host**, `0x49b8e8` = parâmetro de
sessão enviado no `0x30/0xB`, `0x49b8e4` = modo/mundo (escolhe a metade de slots).

## Protocolo de mensagens (observado)

Toda mensagem é um `Send` **garantido** (`flags = 1`, `DPSEND_GUARANTEED`) de um jogador
para **todos** (`idTo = 0`, `DPID_ALLPLAYERS`). Não existe envio não garantido.

O primeiro byte escolhe o tipo: `byte0 & 0xF0`. O despachante é `0x4248c0`; o loop de
`Receive` é `0x4244d0` (trata `DPERR_BUFFERTOOSMALL` realocando).

| Tipo | Handler | O que é |
| --- | --- | --- |
| `0x10` | inline em `0x4248c0` | **Snapshot de entrada**: o host monta e manda ao jogador que acabou de entrar (`0x424580`, caso `DPSYS_CREATEPLAYERORGROUP`). Contém `DAT_0049b8e8` e, para cada **mapa** da metade ativa, blocos serializados (`0x4591d0`, `0x459910`, `0x4598a0`, `0x459a90`), com 25 bytes (`0x19`) por mapa. É o estado do mundo que o recém-chegado precisa |
| `0x20` | `0x423a40` | **Chat**: `0x20` + texto ANSI. Enviado por `0x4239f0` |
| `0x30` | `0x423d80` | **Handshake de jogador**. Nibble baixo `0`: cria o personagem do jogador (`0x423ae0`, classes 1/2/3 = os três heróis, posição em tiles × 32); `0xB`: **relógio da sessão**, grava `DAT_0049b8e8` (o emissor `0x469f82` manda `0x3B` + o contador, 8 bytes, quando `contador & 0x1FF == 0`, isto é, a cada 512 ticks); demais nibbles: **atualização do personagem de um jogador**, aplicada por `0x42ce20` ao objeto guardado com `SetPlayerData` (veja abaixo). Resposta `0x30` de 20 bytes em `0x423d00` |
| `0x40` | `0x423e00` | **Pedido ao host sobre uma entidade**: byte 1 = mapa (< 42), byte 3 = id da entidade. `0x43c810` **só age se `DAT_004998dc` ≠ 0 (sou o host)**: grava a requisição na entidade (`+0x29c = 1`, `+0x2a0` = nibble baixo; para o nibble `5` copia os bytes 2, 4 e 5 como parâmetros). Emissor conhecido: `0x43c980`, mensagem `0x45` de 6 bytes (`0x45`, mapa, id, 3 parâmetros) |
| `0x60` | `0x423e40` | **Stream de comandos por mapa**: byte 1 = índice do mapa (< 42), depois registros de `u16` terminados em `0xFFFF`. Cada registro é decodificado por `0x402cb0` (ver abaixo) e aplicado a uma entidade, ou cria uma entidade nova (mobs pela tabela de 60 entradas em `0x490346`, stride `0x34`; projéteis; itens) |

### Cabeçalho de cada registro `0x60` (`0x402cb0`)

Palavra `w` de 16 bits:

| Bits | Campo |
| --- | --- |
| `15–14` (`w & 0xC000`) | classe: `0x0000` ação em entidade existente; `0x4000` criação/outras operações; |
| `13–7` | **id da entidade** (7 bits, 0–127, único por mapa) |
| `6–3` | subtipo / tipo de ação (4 bits) |
| `2–0` | parâmetro pequeno (3 bits) |

Classe `0x0000`: o subtipo decide o tamanho do payload (0 = 8 bytes; 1, 2, 6, 7 = 2 bytes extras).
Classe `0x4000`: o parâmetro pequeno é o tipo da operação: `2` itens/objetos do mapa
(`0x459c10`), `3` **cria uma entidade** (subtipos 0–4: mob, projétil, item, efeito;
mobs escolhem a classe pela tabela em `0x490346`: tipos `6`, `7`, `8` e os demais), `0`/`1`
`0x459c30`.

### Slots são mapas

Os 42 "slots" de `0x4b9cfc` (`0x2a`) **são os mapas**, não os jogadores: 22 do mundo
(`a1–a14`, `t1–t4`, `c1`, `c2`, `d8`, `d9`) e 20 do submundo (`b1–b7`, `t5`, `t6`, `c3`–`c5`,
`d1–d7`, `d10`), na ordem da tabela de nomes em `0x495a88`. A flag `0x49b8e4` escolhe a
metade ativa (≠0: slots 0–21; 0: slots 22–41). `0x49b8cc` liga o modo multi: com ela zerada
nada é enviado.

### Quem envia o quê, e quando

`0x409438–0x409517` roda no laço do jogo (só se `0x49b8cc` ≠ 0, ou seja, em modo multi).
Para cada mapa carregado da metade ativa faz: zera o buffer (`0x402ba0`), escreve `0x60`
(`0x402bf0`), o índice do mapa, chama o método virtual `+0x24` do mapa (`0x45b740`, vtable
`0x48a7c0`), que descarrega as listas de comandos pendentes de cada contêiner do mapa
(`+0x14aa8`, `+0x14afc` = lista de entidades, `+0x34d3c`, `+0x34d44`, `+0x34d54`…) no buffer
global, e envia o buffer com o terminador `0xFFFF` (`0x402bb0`, que usa `flags = 1` e
`idTo = 0`). O buffer tem limite de 10 000 bytes. Ou seja: **uma mensagem por mapa por
tick**. Os escritores de campo estão nas classes de entidade (`0x43c9xx`, `0x4480xx`,
`0x44e9xx`, `0x44efxx`: 52 chamadas de `append_u8` e 11 de `append_u16`).

### Comandos de personagem (classe `0x0000`, emissor `0x43c9f0`)

`FUN_00402c80(classe, id, ação, tipo)` monta a palavra de 16 bits; o id é o da entidade
(`personagem+0x2c4`), o tipo vem de `personagem+0x52`. O payload depende da ação:

| Ação | Payload | Significado provável |
| --- | --- | --- |
| `0` | 8 bytes: `+0x34`, `x>>5`, `y>>5`, `+0x2f9`, `+0x30a`, `+0x30b`, `+0x30c`, `+0x2c0` | estado completo/entrada na cena (posição em tiles, direção, atributos visuais) |
| `1` | 2 bytes: `x>>5`, `y>>5` | ir para o tile |
| `2` | `u16` de `+0x300` | alvo (id da entidade) |
| `3`, `4`, `5` | nenhum | ações sem dados (parar, esperar…) |
| `6` | `u16` de `+0x300` | alvo (variante) |
| `7` | 2 bytes: `+0x64`, `+0x68` | parâmetros de habilidade/ponto |

### Operações do mapa (classe `0x4000`, parâmetro pequeno = tipo, emissor `0x44ea00`)

O mesmo método **aplica a operação localmente e a enfileira** para os outros (só se
`0x49b8cc ≠ 0`, ou seja, em modo multi). Operações: `0` limpar, `1` item/objeto, `2` **criar
mob**, `3` criar objeto/evento, `4` criar objeto do cenário/item (`< 0x97` = tipo comum,
senão o outro).

Criar mob (`2`): payload de 10 bytes: `mapa`, `x`, `y`, `tipo` (índice na tabela de 60 mobs),
`+0x70`, `+0x74` e **4 bytes derivados do mob recém-criado** (`+0x30a`, `+0x30b`, `+0xC*4`,
`FUN_0043cbb0`). O cliente que cria o mob **rola o aleatório localmente** (3 chamadas de
`rand` para atributos) e manda os valores prontos; os demais **não rolam**, só reproduzem.

### Quem executa o quê (flag de host)

`DAT_004998dc` (sou o host) é consultada em 60 pontos; 38 deles em `0x4384bf–0x446f44` (as
classes de mob) e outros em `0x4582a4/0x458314` (spawn do mapa), `0x44fcdb`, `0x4506e6`,
`0x4321fe`, `0x435ff5` e `0x469f4d/0x469f9c` (relógio). Padrões vistos:

- `if (multi && !host) return` (`0x44fcdb`): lógica só do host ou do single-player;
- `if (host) { consumir pedido +0x29c/+0x2a0 }` (`0x4384bf`): consome os pedidos `0x40`;
- `if (!multi || host) { … }` (`0x4321fe`): idem.

Conclusão: **a IA e o spawn dos mobs rodam no host** e os resultados chegam aos outros pelo
stream `0x60`. Os clientes simulam o próprio personagem.

### Estado dos personagens

Há seis emissores de 6 a 8 bytes (`0x42d3fc`, `0x42d45a`, `0x42d4bc`, `0x42d547`, `0x42d659`,
`0x42d75b`) na classe do personagem. O destinatário é sempre todos; o remetente é o DPID do
dono (`personagem+0x754`). Campos observados: posição em tiles (`x >> 5`, `y >> 5`), HP
(`u16` em `+0x810`), atributos (`+0x798`, `+0x84`, `+0x88`) e um `u32` em `+0x730`. Cada
cliente é, portanto, a **fonte do estado do próprio personagem** (HP, posição, stats) e os
outros apenas aplicam.

### Mensagens de sistema do DirectPlay

Tratadas em `0x424580` (as que chegam com `idFrom = 0`):

- `0x0003` `DPSYS_CREATEPLAYERORGROUP`: o host manda o snapshot `0x10` e o `0x30`;
- `0x0005` `DPSYS_DESTROYPLAYERORGROUP`: remove os personagens do jogador (`0x423cc0`);
- `0x0101` `DPSYS_HOST`: este cliente virou host (`DAT_004998dc = 1`). **O jogo trata migração
  de host**, e a sessão é criada com `DPSESSION_MIGRATEHOST` (confirmado abaixo).

### Sessão (`DPSESSIONDESC2`)

- **Criar** (`0x401ed0`): `dwFlags = 0x44` = `DPSESSION_MIGRATEHOST` (`0x04`) + `DPSESSION_KEEPALIVE`
  (`0x40`); **sem** `DPSESSION_DIRECTPLAYPROTOCOL` (`0x80`), ou seja, usa o protocolo antigo
  do DirectPlay. `Open` com `DPOPEN_CREATE` (`2`). Preenche `dwMaxPlayers`, `lpszSessionName`
  e `dwUser1` (provavelmente o mundo, World/Underworld). `guidApplication` vem de `objeto+0xC`.
- **Entrar** (`0x401e30`): `Open` com `DPOPEN_JOIN` (`1`), com o `guidInstance` da sessão
  escolhida na lista e o mesmo `guidApplication`. Sem senha.

## O que isso diz sobre a arquitetura

- É **peer-to-peer com um host de sessão**: cada cliente manda para todo mundo os comandos
  pendentes de cada mapa que tem carregado; o host só dá o snapshot de entrada e a identidade. Não há servidor
  autoritativo sobre mobs, loot ou dano. **Hipótese** (falta ver quem spawna mobs).
- **Não é lockstep** pelo que o código mostra: não há "esperar o frame N de todos". Cada cliente
  simula e aplica os comandos que chegam. **Hipótese** (falta ver o laço principal).
- Tudo é garantido: no TCP/IP do DirectPlay isso é um canal confiável, então um pacote
  atrasado atrasa os seguintes (head-of-line), mas nunca se perde.
- O protocolo é pequeno (5 tipos). Um relay que entenda o jogo é viável.

## Bug de compatibilidade já corrigido

`WM_INITDIALOG` do diálogo 111 (`0x40d5ac`) faz `LB_ADDSTRING` com `lParam = NULL` para criar a
linha vazia do topo. O `user32` atual dá `wcslen(NULL)` e derruba o jogo. O `patch_akuma.py`
envia `""` no lugar.

## O que ainda não sabemos

1. A IA e o spawn dos mobs rodam no host (60 pontos consultam a flag, ver acima). Falta
   confirmar o loot (drop e pickup) e os NPCs.
2. Frequência do envio (taxa do laço do jogo) e o que ocorre com latência alta.
3. Layout dos payloads dos comandos `0x60` por subtipo e do significado do pedido `0x45`.
4. Se o save local é aceito sem validação (XP, itens, nível). O estado do personagem vem do
   cliente (HP, stats), então **a princípio é confiável cegamente**.
5. Como o `dwUser1` da sessão é usado e qual o limite de jogadores real.

## Próximos passos

1. Confirmar loot e NPCs, e o significado do pedido `0x45` (quem o emite além do `0x43c980`).
2. Descrever o snapshot `0x10` campo a campo (`0x4591d0` e os outros).
3. Capturar tráfego real entre duas instâncias para validar a leitura estática.
4. Prototipar um relay mínimo que repasse as mensagens, e só depois filtrar.

## Captura de tráfego (log de rede)

O build instala dois ganchos: o wrapper `Send` (`0x401fe0`) e a chamada de `Receive`
(`0x4244fc`). Eles chamam `HudNetSend`/`HudNetRecv` na `AkumaHud.dll`, que **só gravam se a
flag estiver ligada**: arquivo `netlog.enable` ao lado do `AkumaHD.exe` ou `AKUMA_NETLOG=1`.

Formato do `netlog.txt`, uma mensagem por linha:

```
<ms> <S|R> <host> <meu_dpid> <de> <para> <flags> <tamanho> <hex dos primeiros 4096 bytes>
```

`S` = enviada, `R` = recebida; `host` é `0x4998dc`; `de = 0` em `R` é mensagem de sistema do
DirectPlay. Para analisar, rodar o jogo em duas máquinas (uma hospeda, outra entra), jogar
alguns minutos, e comparar os dois `netlog.txt`.

## O que o primeiro log mostrou (host, ~6 min, 2026-11-21)

- **Tick de 25 Hz**: o relógio `0x3B` sai a cada 512 ticks, a ~20,5 s (`3bb949` + `u32`).
- **Só o host manda `0x60`** (3056 mensagens enviadas, **0 recebidas**): o visitante nunca gera
  stream de mapa. Só é enviado o que tem registro pendente (o buffer vazio não vai).
- **Dano do visitante = mensagem `0x45`** (6 bytes, `0x40` + nibble 5): `45 <mapa> <direção> <id do
  mob> <dano> <tipo>`. Exemplo: `450c0400 4c 01` = mapa 12, direção 4, mob 0, dano 76, tipo 1.
  Um por golpe (~560 ms), dano `0` quando erra. O visitante **não mexe no HP do mob**: o
  `Mob::TakeDamage` (`0x43E620`) só envia o pedido e retorna (`multi && !host`). O host
  consome o pedido em `0x4384d8` e chama `+0x14` da vtable, que é o mesmo `0x43E620`, agora
  pelo caminho do host.
- Chat: `0x20` + texto, só enviado, 6 mensagens.
- `0x30` com 5 bytes (`0x33`) é o mais frequente (estado do personagem); `0x34`/`0x37` de 8
  bytes, `0x31` de 6, `0x3B` o relógio.

## O que o log do visitante mostrou

- **Migração de host funciona na prática.** Quando o host saiu, o visitante recebeu em
  sequência `DPSYS_DESTROYPLAYERORGROUP` (`05000000…`, 74 bytes) e `DPSYS_HOST`
  (`01010000`), a flag de host foi para 1 e **no mesmo milissegundo** ele começou a mandar
  `0x60` para todos os mapas. Como todo cliente já replica o estado de todos os mapas, a
  simulação continua do ponto em que estava.
- O visitante recebeu 6291 streams `0x60` do host em ~6 min (quase todos de 8 a 24 bytes) e
  mandou 24 pedidos `0x45` (os golpes).
- **HP do herói é replicado**: mensagem `0x37` de 8 bytes:
  `37 <mapa> <id do herói> <x> <y> <byte> <HP u16 LE>`. Exemplo: `370e0f2c21fd0e05` =
  mapa 14, herói 15, tile (44, 33), HP `0x050E` = 1294. Sai quando o HP muda.
- `0x33` (5 bytes): `33 <mapa> <id> <x> <y>`, posição do herói. `0x34` (8 bytes) é o
  comando de mover com destino.
- Cada máquina só tem o próprio herói na lista que o HUD usa para o grupo (heróis locais
  têm `+0x750 ≠ 0`). O herói do visitante aparece como **kind 3** no slot 0, o do host como
  **kind 1**.

## Relay (ideia)

Como todo envio é broadcast garantido, um relay em estrela (cada cliente fala só com o relay)
é natural:

- **Opção A, túnel de transporte**: um provedor de serviço próprio no lugar do TCP/IP do
  DirectPlay, que fala com um relay na nuvem. O jogo e o protocolo ficam intactos e resolve
  NAT e descoberta de sala. É a opção mais próxima do que o PPSSPP faz.
- **Opção B, relay que entende o jogo**: com o protocolo acima dá para validar e filtrar
  (snapshot, chat, comandos), limitar abuso e até persistir personagens no servidor. Mais
  trabalho, mais controle.
- **Opção C, VPN/porta aberta**: sem código; serve de linha de base para medir o ganho.
