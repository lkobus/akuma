"""Build the three indexed language tables from the original English CD."""
import argparse
from pathlib import Path
import re

STAT = re.compile(r"(?:ALL attributes|Dexterity|Intelligence|Strength|Health|Skill|Magic|Life|Mana|Sight|Dex|M\.Speed|A\.Speed|Attack)(?=[+-])", re.IGNORECASE)
STATS = [
    ("All attributes", "Todos os atributos", "모든 능력치"),
    ("Dexterity", "Destreza", "민첩"),
    ("Intelligence", "Inteligência", "지능"),
    ("Strength", "Força", "힘"),
    ("Health", "Vigor", "체력"),
    ("Skill", "Hab", "기술"),
    ("Magic", "Magia", "마력"),
    ("Life", "Vida", "생명력"),
    ("Mana", "Mana", "마나"),
    ("Sight", "Visão", "시야"),
    ("Dex", "Des", "민첩"),
    ("M.Speed", "Mov.", "이동 속도"),
    ("A.Speed", "Vel. Atq.", "공격 속도"),
    ("Attack", "Ataque", "공격"),
]
LANGUAGES = [("pt", "cp1252"), ("ko", "cp949"), ("en", "ascii")]


def catalog(file):
    translations = {}
    for line in file.read_text(encoding="utf-8-sig").splitlines():
        if not line or line.startswith("#"):
            continue
        index, *values = line.split("|")
        index = int(index)
        if not 1 <= index <= 541 or index in translations or len(values) not in (2, 3):
            raise ValueError("Entrada de idioma inválida: " + line)
        translations[index] = values
    return translations


def generate(folder, translations):
    source = (folder / "lang.gos").read_bytes().decode("ascii").splitlines()
    if len(source) != 541 or source[0] != "Place to drop items" or source[501] != "Western Village":
        raise ValueError("A tabela de idioma não corresponde ao CD inglês suportado.")
    stats = {name.lower(): (pt, ko, name) for name, pt, ko in STATS}
    for lang, (tag, encoding) in enumerate(LANGUAGES):
        lines = []
        for index, english in enumerate(source, 1):
            values = translations.get(index, [])
            if lang < len(values):
                localized = values[lang]
            elif lang < 2 and (138 <= index <= 487):
                localized = STAT.sub(lambda m: stats[m.group().lower()][lang], english)
            else:
                localized = english
            line = localized.encode(encoding)
            # Short equipment labels are copied into 20-byte native slots.
            limit = 64
            if 285 <= index <= 487 and len(english) <= 19 and index in translations:
                limit = max(19, len(english))
            if len(line) > limit or b"\0" in line:
                raise ValueError("Texto comprido demais em {}:{}".format(tag, index))
            lines.append(line)
        (folder / ("lang-" + tag + ".gos")).write_bytes(b"\r\n".join(lines) + b"\r\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folder", type=Path)
    args = parser.parse_args()
    generate(args.folder, catalog(Path(__file__).with_name("translations.txt")))
