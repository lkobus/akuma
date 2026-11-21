"""Apply NPC dialogue translations without changing event control flow."""
import argparse
import hashlib
import json
from pathlib import Path
import re


EVENTS = ("sam.evt", "under.evt", "multi1.evt", "multi2.evt")
LANGS = (("pt", "cp1252"), ("ko", "cp949"), ("en", "ascii"))
# Match only player-visible parameters. Event names, flags and jump labels are
# keys in the script and must keep their original bytes.
COMMAND = re.compile(rb'^[ \t]*(?:@[ \t]*(?:TALK|FACE|ORDER)\b|(?:CASE|MENT)\b)[^"\r\n]*"([^"\r\n]+)"',
                     re.IGNORECASE)
TALK = re.compile(rb'^[ \t]*@[ \t]*TALK\b', re.IGNORECASE)
SPEAKERS = {
    "Blacksmith": ("Ferreiro", "대장장이"),
    "Shopkeeper": ("Comerciante", "상점 주인"),
    "Item Merchant": ("Mercador", "아이템 상인"),
    "Village Chief": ("Chefe da Vila", "마을 촌장"),
    "Witch": ("Bruxa", "마녀"),
    "Jeweler": ("Joalheiro", "보석상"),
    "Trainer": ("Instrutor", "훈련관"),
    "Retired General": ("General Aposentado", "은퇴한 장군"),
    "White Lady": ("Dama Branca", "백의 부인"),
    "Eastern Demon": ("Demônio do Leste", "동쪽 악마"),
    "Western Demon": ("Demônio do Oeste", "서쪽 악마"),
}


def payloads(data):
    for line in data.splitlines(keepends=True):
        m = COMMAND.match(line)
        if not m:
            yield line, None, None
            continue
        source = m[1].decode("cp949").strip()
        # Skip English-independent placeholders, punctuation and parser tokens.
        if not source or not source.isascii() or source.upper() == "HERO" or not any(c.isalpha() for c in source):
            yield line, None, None
            continue
        yield line, m, source


def key(source):
    return hashlib.sha256(source.encode("ascii")).hexdigest()


def clean(value, encoding, limit):
    value = value.replace("’", "'").replace("‘", "'").replace("“", "'").replace("”", "'")
    value = value.replace('"', "'").replace("…", "...").replace("—", "-").replace("–", "-")
    value = " ".join(value.split())
    try:
        encoded = value.encode(encoding)
    except UnicodeEncodeError:
        return None
    if not encoded:
        return None
    if len(encoded) <= limit:
        return encoded
    # The engine's dialogue box is fixed width. Reduce long machine drafts
    # at a word boundary while keeping the EVT's number and order of records.
    while len(value.encode(encoding, errors="replace")) > limit - 3:
        shorter, sep, _ = value.rpartition(" ")
        value = shorter if sep else value[:-1]
    return (value + "...").encode(encoding, errors="replace")


def variant(data, lang_index, encoding, catalog):
    lines = []
    for line, match, source in payloads(data):
        if source is not None:
            texts = SPEAKERS.get(source) if lang_index < 2 else None
            texts = texts or catalog.get(key(source))
            text = texts[lang_index] if texts is not None and len(texts) > lang_index else None
            if text:
                limit = 62 if TALK.match(line) else 55
                if source.startswith("<") and source.endswith(">"):
                    text = "<" + text.strip(" <>") + ">"
                localized = clean(text, encoding, limit)
                if localized is not None:
                    line = line[:match.start(1)] + localized + line[match.end(1):]
        lines.append(line)
    return b"".join(lines)


def build(folder, catalog):
    for name in EVENTS:
        path = folder / name
        original = path.read_bytes()
        for i, (tag, encoding) in enumerate(LANGS):
            path.with_stem(path.stem + "-" + tag).write_bytes(variant(original, i, encoding, catalog))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folder", type=Path)
    parser.add_argument("catalog", type=Path)
    args = parser.parse_args()
    build(args.folder, json.loads(args.catalog.read_text(encoding="utf-8-sig")))

