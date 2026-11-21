# Akuma — Demon Spawn para Windows

Patch de compatibilidade e remaster de interface para a edição inglesa em CD de
**Akuma — Demon Spawn (Lamentation Sword)**. O build extrai os dados do **seu**
BIN e gera um ZIP portátil com:

- launcher (`Jogar.exe`) com presets de vídeo, idioma e controles;
- janela redimensionável, tela cheia sem bordas e filtros gráficos (cnc-ddraw);
- HUD lateral em 16:9, minimapa arrastável, zoom de câmera e contador de FPS;
- português, inglês e coreano para menus, interface, mapas e diálogos de NPCs.

> **O repositório contém só código e traduções.** O patch pronto fica na aba
> **Releases** e o jogo base (cópia fiel dos arquivos do CD) fica no Discord.
> Projeto de fãs, sem vínculo com os detentores dos direitos de Akuma.

## Como jogar (3 passos)

1. Baixe o **jogo base** (`Akuma-Original.zip`) no nosso Discord e extraia.
   Fica uma pasta `Akuma` com o jogo original, sem instalar nada e sem montar ISO.
2. Baixe o **`Akuma-Patch.zip`** da [última release](../../releases/latest) e
   extraia **dentro da pasta `Akuma`**, aceitando substituir os arquivos.
3. Abra `Jogar.exe`, escolha idioma e vídeo e clique em Jogar.

Os controles estão em [LEIA-ME.txt](LEIA-ME.txt).

## Como compilar do código-fonte (para quem vai modificar)

Requisitos: Windows, Python 3.9+, [Zig 0.14.1](https://ziglang.org/download/)
e o wrapper [cnc-ddraw v7.1.0.0](https://github.com/FunkyFr3sh/cnc-ddraw/releases/tag/v7.1.0.0)
(`ddraw.dll`, `Shaders` e `LICENSE.txt` extraídos numa pasta).

```powershell
python -m pip install -r requirements.txt
.\build.ps1 -Image 'C:\Midia\AKUMA.bin' -CncDdraw 'C:\Ferramentas\cnc-ddraw'
```

A saída é `dist\Akuma-Portatil.zip`. Extraia e abra `Jogar.exe`. Esse build lê
direto do seu BIN/CUE, então não depende do jogo base do Discord.
Opções úteis: `-Zig` (caminho do `zig.exe`), `-SlayersImg` (pasta `img` do
Slayers, de onde vem a arte reaproveitada no HUD; padrão `C:\Taewool\Slayers\img`)
e `-Output`. O build confere o hash do executável do CD e da DLL.

## Estrutura

| Arquivo | Função |
| --- | --- |
| `build.ps1` | Orquestra todo o build e monta o ZIP |
| `make_overlay.ps1` | Compara o build com a pasta original e gera o `Akuma-Patch.zip` (só o que mudou) |
| `extract_cd.py` | Extrai a pasta do jogo da trilha MODE2/2352 do BIN |
| `patch_akuma.py` | Adapta o EXE (DirectDraw local, player de vídeo, contador de FPS) |
| `Launcher.cs` | Launcher em C# (.NET Framework 4, compilado com o `csc` do Windows) |
| `AkumaHud.c` | DLL x86 do HUD, minimapa e câmera, compilada com `zig cc` |
| `hud_skin.py`, `quest_starts.py` | Geram cabeçalhos C a partir dos dados extraídos |
| `localize.py`, `menu_labels.py`, `localize_dialogues.py` | Idiomas |
| `translations.txt`, `dialogues.json` | Textos traduzidos (a edição é por aqui) |
| `docs/widescreen.md` | Como o motor foi investigado e os endereços usados |

## Como modificar o projeto

O ciclo é sempre: **editar → rodar `build.ps1` → abrir `Jogar.exe` → testar**.

| Quero... | Edite |
| --- | --- |
| Corrigir traduções de interface, itens e mapas | `translations.txt` (`índice\|português\|한국어\|English`) |
| Corrigir falas de NPCs | `dialogues.json` (chave = hash da fala original, valor = `[português, coreano]`) |
| Mudar o launcher (opções, presets) | `Launcher.cs` |
| Mudar HUD, minimapa, câmera, atalhos | `AkumaHud.c` |
| Mudar patches no executável | `patch_akuma.py` (os endereços valem para a revisão conferida pelo build) |
| Mudar a configuração do wrapper de vídeo | `ddraw.ini` |

Dicas:

- Para testar rápido, compile só a parte que mexeu. Exemplo com o launcher:
  `csc /target:winexe /platform:x86 /out:Jogar.exe Launcher.cs`.
- Leia [docs/widescreen.md](docs/widescreen.md) antes de mexer em câmera, HUD
  ou resolução.
- Nunca faça commit de arquivos extraídos do CD, do `dist\` ou de ZIPs.
  O `.gitignore` bloqueia a maioria e o CI recusa os que escaparem.

### Modificando com IA

Pode usar qualquer assistente de código (Claude Code, Codex, Copilot, Cursor,
ChatGPT...). Funciona bem porque o projeto é pequeno e cada arquivo tem um papel
claro. Para um bom resultado:

1. Dê ao assistente o `README.md`, o `docs/widescreen.md` e **só** os arquivos
   que a mudança toca.
2. Descreva o que quer em termos de comportamento no jogo, não de código
   ("o minimapa deve lembrar a posição ao fechar o jogo").
3. Peça para ele rodar `build.ps1` e explicar como você deve testar.
4. Mudanças em `patch_akuma.py` mexem em bytes do executável: peça sempre os
   endereços e a justificativa, e confira no jogo.
5. **Revise o diff.** Você é o autor do PR e responde por ele.

O arquivo [docs/modificando-com-ia.md](docs/modificando-com-ia.md) tem um prompt
inicial pronto e exemplos. Arquivos de configuração do seu assistente
(`CLAUDE.md`, `AGENTS.md`, `.cursorrules`, `.codex/`...) ficam no seu
computador: o `.gitignore` já os ignora.

## Contribuindo

A branch `main` é protegida: ninguém comita nela diretamente.
Faça um fork, crie uma branch, abra um Pull Request e preencha o checklist.
O CI precisa passar e o dono do projeto precisa aprovar. Detalhes em
[CONTRIBUTING.md](CONTRIBUTING.md).

## Releases

Cada release traz o **`Akuma-Patch.zip`**: só os arquivos novos ou alterados
(executável, HUD, launcher, wrapper de vídeo, idiomas e diálogos), cerca de 4 MB.
Ele é feito para ser extraído sobre a pasta do jogo base. Quem mantém o projeto
gera o patch com `make_overlay.ps1` (veja [docs/mantenedor.md](docs/mantenedor.md)).

## Licença

O código e as traduções deste repositório usam a [licença MIT](LICENSE).
O jogo e seus arquivos pertencem aos respectivos detentores; o patch os modifica
para compatibilidade com o Windows atual.
O ZIP gerado inclui o [cnc-ddraw](https://github.com/FunkyFr3sh/cnc-ddraw)
(licença própria, copiada junto como `LICENSE-cnc-ddraw.txt`).
