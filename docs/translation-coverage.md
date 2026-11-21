# Translation coverage (what is localized, where it lives)

Result of scanning the original `Akuma.exe` (resources, `.rdata`, `.data`) and the data files.
Languages: pt (Portuguese), en (English), ko (Korean), zh (Traditional Chinese, Big5).
The scan script is `.work/scan_strings.py` (not tracked); it writes `.work/scan_report.json`.

| Where the text lives | Source of translation | Covered |
|---|---|---|
| `img/lang.gos` (541 strings: menus, tooltips, items, spells, monsters, maps, stats) | `translations.txt` (pt/ko/en), `translations-zh.txt` (zh) → `localize.py` | pt, ko, en, zh. In zh, invented proper names (Glucyn, Hellminor…) stay in English. |
| Menu and map banners (`menuicon*.gos`, `icon1*.gos`) | `menu_labels.py` (rendered with the language's font) | pt, ko, en, zh |
| NPC dialogue (`sam`, `under`, `multi1`, `multi2` `.evt`) | `dialogues.json` (pt/ko), `dialogues-zh.json` (zh), `localize_dialogues.py` | pt, ko, en (original), zh (machine draft + hand-written choices and speaker names) |
| Launcher (`Jogar.exe`) | `Launcher.cs` | pt, en, ko, zh (captions) |
| Remastered HUD texts (quest panel, party list, drop hint) | `AkumaHud.c` tables | pt, en, ko, zh |
| Native status line under the world (ATK/DEF/…) | ANSI text redrawn by the HUD; converted with the language's code page | all |
| **Gameplay notices** in the engine (`No room to take this item`, `Overflow the limit of weight to take`, `Not in the same region`, `The Teleport Gate of the other side is not operated`, `No place to take this item`, `No Weapons`) | `AkumaHud.c` `game_messages` | pt, ko, zh (these were English in every language before) |
| **Multiplayer dialogs** (Windows dialog resources 109–112 in the English exe: `SELECT CHARACTER`, `MULTIPLAY SETTING`, `Select Host`, `Find Host`, `Hosting`, `Host IP Address`, …) | `AkumaHud.c` `dialog_texts`, applied on `WM_INITDIALOG` | pt, ko, zh. Korean text comes from the original Korean dialogs 105–108 that ship in the same exe. |

## Not localized on purpose

- `Cannot load …gos`, `Failed in initializing DirectX`, `Runtime Error!` and other developer or C runtime errors.
- `Loading . . .` on the loading screen and `LEVEL %d` (format string in the exe).
- Obsolete item names left in the exe's data (`Accient Sword`, `Chines Sword`): the game reads names from `lang.gos`.
- The manual (`manual/*.htm`) is English only.

## Known gaps

- zh dialogue is machine-translated; it needs a native reviewer.
- Images with baked-in text other than the menu banners (title screen, `story.gos`) are not regenerated.
- The `Connecting…` / `Finding…` messages are in `lang.gos` and covered, but the DirectPlay provider names in the connection list come from Windows.
