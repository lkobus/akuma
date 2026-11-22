# zh-original

Files from the original Traditional Chinese (zhTW) release of Akuma, sent by the community member nGuC:

- `sam-zh.evt`, `under-zh.evt`, `multi1-zh.evt`, `multi2-zh.evt`: the NPC scripts and dialogue, in Big5.
- `lang-zh.gos`: the interface, item and map string table.
- `icon1-zh.gos`, `icon10-zh.gos`: the map name banners.

`build.ps1` and `dev-build.ps1 -Localize` copy them over the files generated from `translations-zh.txt`
and `dialogues-zh.json`, so the Chinese version uses the original zhTW text. They are the work of the
original publisher and are included here, with the maintainer's agreement, as a restoration: the
Western (English CD) data with the Chinese release's text on top.

The scripts come from a slightly different revision of the game (a few dozen logic commands differ per file
from the English CD scripts), so play the main quests after changing them. To go back to the generated
(machine-translated) Chinese, remove these files.
