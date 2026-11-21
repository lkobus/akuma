"""Generate localized menu labels for the original Akuma sprite dimensions."""
import argparse
import os
from pathlib import Path
import struct

from PIL import Image, ImageDraw, ImageFont


LABELS = {
    "pt": ("Campanha", "Multijogador", "Sair do jogo", "Novo jogo",
           "Carregar jogo", "Menu principal", "Salvar jogo", "Carregar jogo",
           "Opções", "Voltar ao menu", "Continuar"),
    "ko": ("싱글 플레이", "멀티플레이", "게임 종료", "새 게임",
           "게임 불러오기", "메인 메뉴", "게임 저장", "게임 불러오기",
           "설정", "메뉴로 돌아가기", "계속하기"),
    "en": ("Single Player", "Multiplayer", "Quit Game", "New Game",
           "Load Game", "Main Menu", "Save Game", "Load Game",
           "Options", "Return to Menu", "Resume Game"),
}
LABEL_BLOCKS = (0, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11)


def font_path(tag):
    path = Path(os.environ["WINDIR"]) / "Fonts" / ("malgunbd.ttf" if tag == "ko" else "segoeuib.ttf")
    if not path.is_file():
        raise FileNotFoundError("Fonte de menu do Windows ausente: " + str(path))
    return path


def draw_label(text, width, height, font, hovered):
    image = Image.new("RGBA", (width, height))
    draw = ImageDraw.Draw(image)
    size = min(29 if height >= 64 else 25, height - 14)
    while True:
        face = ImageFont.truetype(str(font), size)
        left, top, right, bottom = draw.textbbox((0, 0), text, font=face, stroke_width=1)
        if right - left <= width - 14:
            break
        size -= 1
    x = (width - (right - left)) // 2 - left
    y = (height - (bottom - top)) // 2 - top
    draw.text((x, y + 2), text, font=face, stroke_width=2, fill="#25161a", stroke_fill="#25161a")
    draw.text((x, y), text, font=face, stroke_width=1,
              fill="#ffe7a3" if hovered else "#f5eae3",
              stroke_fill="#5f291b" if hovered else "#2e1920")
    return image


def encode(image, rgb555):
    """Raw-color runs with 0xFF reserved for the end of each row."""
    pixels = image.load()
    out = bytearray()
    shift = 5 if rgb555 else 6
    for y in range(image.height):
        x = last = 0
        while x < image.width:
            if pixels[x, y][3] < 96:
                x += 1
                continue
            start = x
            run = bytearray()
            while x < image.width and pixels[x, y][3] >= 96:
                r, g, b, _ = pixels[x, y]
                color = ((r >> 3) << (5 + shift)) | ((g >> (8 - shift)) << 5) | (b >> 3)
                run.extend(struct.pack("<H", min(color, 0xFFFE)))
                x += 1
            out.extend((start - last, x - start))
            out.extend(run)
            last = x
        out.append(0xFF)
    return out


def rewrite_bank(original, rgb555, replacements):
    out = bytearray(original[:2])
    offset = 2
    block = 0
    while offset < len(original) and original[offset] != 0xFF:
        count, width, height = struct.unpack_from("<BHH", original, offset)
        out.extend(original[offset:offset + 28])
        offset += 28
        for frame in range(count):
            size, flag = struct.unpack_from("<IB", original, offset)
            end = offset + 5 + size
            image = replacements(block, frame, width, height)
            if image is not None:
                if width > 254 or height > 80:
                    raise ValueError("Dimensão não suportada para um rótulo GOS.")
                rle = encode(image, rgb555)
                out.extend(struct.pack("<IB", len(rle), flag))
                out.extend(rle)
            else:
                out.extend(original[offset:end])
            offset = end
        block += 1
    if original[offset:offset + 1] != b"\xff":
        raise ValueError("Terminador do banco de sprites ausente.")
    return out + original[offset:]


def area_label(text, width, height, font, color):
    image = Image.new("RGBA", (width, height))
    draw = ImageDraw.Draw(image)
    for size in range(15, 8, -1):
        face = ImageFont.truetype(str(font), size)
        rows = [""]
        for word in text.split():
            wrapped = (rows[-1] + " " + word).strip()
            if rows[-1] and draw.textlength(wrapped, font=face) > width - 4:
                rows.append(word)
            else:
                rows[-1] = wrapped
        if len(rows) <= 3 and all(draw.textlength(row, font=face) <= width - 4 for row in rows):
            break
    label = "\n".join(rows)
    left, top, right, bottom = draw.multiline_textbbox((0, 0), label, font=face, spacing=1)
    draw.multiline_text(((width - (right - left)) // 2 - left, 2 - top), label,
                        font=face, fill=color, spacing=1, align="center", stroke_width=1,
                        stroke_fill="#322519")
    return image


def menu_bank(source, tag, rgb555):
    labels = dict(zip(LABEL_BLOCKS, LABELS[tag]))
    font = font_path(tag)

    def replacements(block, frame, width, height):
        if block in labels:
            return draw_label(labels[block], width, height, font, frame != 0)
        return None

    return rewrite_bank(source, rgb555, replacements)


def map_bank(source, names, tag, rgb555):
    font = font_path(tag)

    def replacements(block, frame, width, height):
        if block in (17, 18, 19, 20):
            index = frame + (22 if block in (18, 20) else 0)
            color = "#f8aa42" if block in (17, 18) else "#5edd55"
            return area_label(names[index], width, height, font, color)
        return None

    return rewrite_bank(source, rgb555, replacements)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folder", type=Path)
    args = parser.parse_args()
    for name, rgb555 in (("menuicon", False), ("menuicon0", True)):
        source = (args.folder / (name + ".gos")).read_bytes()
        for lang in LABELS:
            (args.folder / (name + "-" + lang + ".gos")).write_bytes(menu_bank(source, lang, rgb555))
    for name, rgb555 in (("icon1", False), ("icon10", True)):
        source = (args.folder / (name + ".gos")).read_bytes()
        for lang, encoding in (("pt", "cp1252"), ("ko", "cp949"), ("en", "ascii")):
            localized = (args.folder / ("lang-" + lang + ".gos")).read_bytes().decode(encoding)
            names = localized.splitlines()[487:529]
            if len(names) != 42:
                raise ValueError("A lista de nomes dos mapas deve ter 42 entradas.")
            (args.folder / (name + "-" + lang + ".gos")).write_bytes(map_bank(source, names, lang, rgb555))
