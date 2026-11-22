# Chinese support (Traditional / zh-TW)

**Status: supported.** The launcher has a Taiwan flag that starts the game with the `zh` language: Big5
font charset, `Microsoft JhengHei`, code page 950. The Chinese text is the original zhTW release's: its NPC
scripts and dialogue, interface table and map banners live in [`zh-original/`](../../zh-original) and are
copied over the generated files by the build, with the Western (English CD) data underneath. They were
supplied by the community member nGuC, who also proofread `translations-zh.txt`, the generated fallback
(`translations-zh.txt` plus the machine-drafted `dialogues-zh.json`) used when those files are removed.

The original scripts are from a slightly different revision of the game than the English CD (a few dozen
logic commands per file differ), so play the main quests after changing them.

## Why a Big5 `.evt` shows garbage

The game stores text as plain bytes and draws it with Windows GDI. GDI decides how to read those
bytes from the **font's character set**, not from the file. The original engine always asks for
the Western (ANSI) character set, so:

- A Korean file only works because the patch hooks the game's font creation and, when the
  language is Korean, switches the font to the Hangul charset and `Malgun Gothic`.
- A Traditional Chinese file is encoded in Big5 (code page 950). With the Western charset, each
  two-byte Chinese character is read as two unrelated Latin characters, so you get garbage no
  matter which `.evt` you use.

So replacing the file is not enough; the patch needs a Chinese language slot of its own.

## How languages work today

| Piece | What it does |
|---|---|
| `Jogar.exe` (launcher) | Passes the language as a two-letter argument (`pt`, `en`, `ko`) and as `AKUMA_LANGUAGE`. |
| `patch_akuma.py` | Rewrites the game's file names (`sam-en.evt` → `sam-<lang>.evt`, `lang-<lang>.gos`, `icon*-<lang>.gos`, `menuicon*-<lang>.gos`) and, for `ko`, forces the Hangul font charset. |
| `localize.py` / `translations.txt` | Builds `lang-<lang>.gos` (541 interface, item and map strings). |
| `localize_dialogues.py` / `dialogues.json` | Builds `sam`, `under`, `multi1`, `multi2` `.evt` per language. |
| `AkumaHud.c` | Draws the remastered HUD; converts item names with code page 1252, or 949 for Korean. |

## What adding zh-TW took (done, except full translation)

1. **Language slot `zh`**: accept it in the launcher and in the patched start-up code, and map it
   to the Big5 charset (`CHINESEBIG5_CHARSET`, 136) with a font that has Traditional glyphs
   (`Microsoft JhengHei`).
2. **HUD**: use code page 950 for item names in `AkumaHud.c`.
3. **Text build**: add `("zh", "cp950")` to the language lists in `localize.py`,
   `localize_dialogues.py` and `menu_labels.py`, plus a Chinese column in `translations.txt` and
   `dialogues.json`.
4. **Launcher**: a Taiwan flag next to the others.
5. **Images**: menu and title graphics that contain text (`menuicon*`, `icon*`) need Chinese
   versions too.

## Limits a translation has to respect

- Text is stored in **bytes**, and every Chinese character is 2 bytes in Big5. Dialogue lines hold
  62 bytes (about 31 characters) for `@TALK` and 55 bytes for other lines; interface strings hold
  64 bytes.
- Short equipment names are copied into 20-byte native slots, so they must be very short.
- Only characters that exist in Big5 can be used. Anything else (curly quotes, some rare
  characters) cannot be encoded and has to be replaced.
- Do not translate event names, flags or jump labels in the `.evt` scripts; only the visible
  strings.

## How to help

- **Translators**: send the Traditional Chinese text as UTF-8. For the interface use the layout of
  `translations.txt` (`index|Portuguese|Korean|English`, with a Chinese column added); for NPC
  dialogue use `dialogues.json` (key = hash of the original English line).
- **Players who already have Big5 files**: tell us where they came from and attach them to an
  issue. They save a lot of work, as long as we can redistribute them.
- **Developers**: the steps above are small and mostly mechanical; see the main repository.

Not tested in game yet. HUD-only texts (quest panel, party list) still show English in Chinese.
