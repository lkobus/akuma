https://discord.gg/2vXyN2HXcQ

# Akuma — Demon Spawn for Windows

🇬🇧 English · 🇧🇷 [Português](README.pt-BR.md)

A compatibility patch and interface remaster for the English CD edition of
**Akuma — Demon Spawn (Lamentation Sword)**. It makes the 2000s-era game run
on modern Windows, with:

- a launcher (`Jogar.exe`) with video presets, language and control options;
- a resizable window, borderless fullscreen and graphics filters (cnc-ddraw);
- a 16:9 side HUD, a draggable minimap, camera zoom and an FPS counter;
- CD music played from files, so no disc or ISO is needed;
- Portuguese, English and Korean for menus, interface, maps and NPC dialogue.

> **This repository only contains code and translations.** The ready-made patch
> is published under **Releases**, and the base game (a faithful copy of the CD
> files) is shared on our Discord. This is a fan project with no affiliation to
> the rights holders of Akuma.

## How to play (3 steps)

1. Download the **base game** (`Akuma-Original.zip`) from our Discord and
   extract it. You get an `Akuma` folder with the original game and the
   soundtrack in `Music`. Nothing to install, no ISO to mount.
2. Download **`Akuma-Patch.zip`** from the [latest release](../../releases/latest)
   and extract it **inside the `Akuma` folder**, replacing files when asked.
3. Run `Jogar.exe`, pick your language and video options, and click *Jogar* (Play).

Controls and shortcuts are listed in [LEIA-ME.txt](LEIA-ME.txt) (Portuguese).

## Building from source (for modders)

Requirements: Windows, Python 3.9+, [ffmpeg](https://ffmpeg.org/) with libvorbis
(for the music), [Zig 0.14.1](https://ziglang.org/download/) and the
[cnc-ddraw v7.1.0.0](https://github.com/FunkyFr3sh/cnc-ddraw/releases/tag/v7.1.0.0)
wrapper (`ddraw.dll`, `Shaders` and `LICENSE.txt` extracted into one folder).
You also need your own copy of the game disc image (`AKUMA.bin` + `AKUMA.cue`).

```powershell
python -m pip install -r requirements.txt
.\build.ps1 -Image 'C:\Media\AKUMA.bin' -CncDdraw 'C:\Tools\cnc-ddraw'
```

The output is `dist\Akuma-Portatil.zip`. Extract it and run `Jogar.exe`. This
build reads straight from your BIN/CUE, so it does not need the Discord base game.

Useful options: `-Music` (folder with already extracted tracks), `-Ffmpeg`,
`-Zig` (path to `zig.exe`), `-SlayersImg` (the Slayers `img` folder that supplies
artwork reused by the HUD; default `C:\Taewool\Slayers\img`) and `-Output`.
The build verifies the hash of the CD executable and of the wrapper DLL.

## Project layout

| File | Purpose |
| --- | --- |
| `build.ps1` | Runs the whole build and assembles the ZIP |
| `make_overlay.ps1` | Diffs the build against the original folder and writes `Akuma-Patch.zip` (changed files only) |
| `extract_cd.py` | Extracts the game folder from the BIN's MODE2/2352 data track |
| `extract_audio.py` | Extracts the CUE/BIN audio tracks to `Music\trackNN.ogg` (uses ffmpeg) |
| `CdAudio.c` | CD music emulation: plays `Music\*.ogg` in place of the disc (uses `third_party/stb_vorbis.c`) |
| `patch_akuma.py` | Adapts the EXE (local DirectDraw, video player, FPS counter) |
| `Launcher.cs` | C# launcher (.NET Framework 4, built with the `csc` that ships with Windows) |
| `AkumaHud.c` | x86 DLL for the HUD, minimap and camera, built with `zig cc` |
| `hud_skin.py`, `quest_starts.py` | Generate C headers from the extracted data |
| `localize.py`, `menu_labels.py`, `localize_dialogues.py` | Languages |
| `translations.txt`, `dialogues.json` | Translated text (edit these to fix translations) |
| `docs/widescreen.md` | How the engine was investigated, with the addresses used (Portuguese) |

## How to modify the project

The loop is always: **edit → run `build.ps1` → run `Jogar.exe` → test**.

| I want to... | Edit |
| --- | --- |
| Fix interface, item or map translations | `translations.txt` (`index\|Portuguese\|Korean\|English`) |
| Fix NPC dialogue | `dialogues.json` (key = hash of the original line, value = `[Portuguese, Korean]`) |
| Change the launcher (options, presets) | `Launcher.cs` |
| Change the HUD, minimap, camera or hotkeys | `AkumaHud.c` |
| Change CD music behavior | `CdAudio.c` |
| Change executable patches | `patch_akuma.py` (addresses are valid for the executable revision the build checks) |
| Change the video wrapper configuration | `ddraw.ini` |

Tips:

- To test quickly, build only the part you touched. For the launcher:
  `csc /target:winexe /platform:x86 /out:Jogar.exe Launcher.cs`.
- Read [docs/widescreen.md](docs/widescreen.md) before touching the camera, HUD
  or resolution.
- Never commit files extracted from the CD, `dist\` or ZIPs. `.gitignore` blocks
  most of them and the CI rejects the rest.

### Modifying with AI

Any coding assistant works (Claude Code, Codex, Copilot, Cursor, ChatGPT...).
The project is small and each file has a clear role, which suits AI well. For
good results:

1. Give the assistant `README.md`, `docs/widescreen.md` and **only** the files the
   change touches.
2. Describe what you want as behavior in the game, not as code
   ("the minimap should remember its position when the game closes").
3. Ask it to run `build.ps1` and explain how you should test.
4. Changes to `patch_akuma.py` edit bytes of the executable: always ask for the
   addresses and the reasoning, and verify in the game.
5. **Review the diff.** You are the author of the PR and responsible for it.

[docs/modificando-com-ia.md](docs/modificando-com-ia.md) (Portuguese) has a
ready-to-use starter prompt and examples. Your assistant's own configuration
files (`CLAUDE.md`, `AGENTS.md`, `.cursorrules`, `.codex/`...) stay on your
machine: `.gitignore` already excludes them.

## Contributing

The `main` branch is protected: nobody commits to it directly. Fork the repo,
create a branch, open a Pull Request and fill in the checklist. CI must pass and
the project owner must approve. See [CONTRIBUTING.md](CONTRIBUTING.md).

## Releases

Each release ships **`Akuma-Patch.zip`**: only the new or changed files
(executable, HUD, launcher, video wrapper, languages and dialogue), about 4 MB.
It is meant to be extracted over the base game folder. Maintainers generate it
with `make_overlay.ps1` (see [docs/mantenedor.md](docs/mantenedor.md)).

## License

The code and translations in this repository are under the [MIT license](LICENSE).
The game and its files belong to their respective rights holders; the patch
modifies them for compatibility with current Windows.
`third_party/stb_vorbis.c` is public domain (Sean Barrett).
The generated ZIP includes [cnc-ddraw](https://github.com/FunkyFr3sh/cnc-ddraw)
(its own license, shipped alongside as `LICENSE-cnc-ddraw.txt`).
