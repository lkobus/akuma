"""Bake the Akuma CD's panel textures into the portable HUD helper."""

import argparse
from pathlib import Path
import struct

from PIL import Image, ImageEnhance, ImageOps


def write_pixels(header, name, image):
    pixels = image.load()
    header.write(f"static const uint16_t {name}[{image.height}][{image.width}] = {{\n")
    for y in range(image.height):
        header.write("    {")
        for x in range(image.width):
            red, green, blue = pixels[x, y]
            value = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
            header.write(f"0x{value:04x},")
        header.write("},\n")
    header.write("};\n")


def write_keyed_pixels(header, name, image):
    """Write an RGBA decoration with 0xffff as the transparent color key."""
    pixels = image.convert("RGBA").load()
    header.write(f"static const uint16_t {name}[{image.height}][{image.width}] = {{\n")
    for y in range(image.height):
        header.write("    {")
        for x in range(image.width):
            red, green, blue, alpha = pixels[x, y]
            value = 0xFFFF if alpha < 128 else ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
            if value == 0xFFFF and alpha >= 128:
                value = 0xFFDF
            header.write(f"0x{value:04x},")
        header.write("},\n")
    header.write("};\n")


def cos_frames(path, wanted_block):
    """Decode one Slayers COS sprite block using the game's indexed row RLE."""
    data = path.read_bytes()
    palette = []
    for index in range(256):
        color, = struct.unpack_from("<H", data, index * 2)
        palette.append(((color >> 11 & 31) * 255 // 31,
                        (color >> 5 & 63) * 255 // 63,
                        (color & 31) * 255 // 31, 255))

    offset = 0x200
    block = 0
    while offset < len(data) and data[offset] != 0xFF:
        count = data[offset]
        width, height, _, _ = struct.unpack_from("<HHHH", data, offset + 1)
        offset += 9
        decoded = []
        for _ in range(count):
            size, = struct.unpack_from("<H", data, offset)
            payload = data[offset + 3:offset + 3 + size]
            offset += 3 + size
            if block != wanted_block:
                continue
            image = Image.new("RGBA", (width, height))
            pixels = image.load()
            source = 0
            for y in range(height):
                x = 0
                while payload[source] != 0xFF:
                    skip, run = payload[source:source + 2]
                    source += 2
                    x += skip
                    for _ in range(run):
                        if x < width:
                            pixels[x, y] = palette[payload[source]]
                        source += 1
                        x += 1
                source += 1
            decoded.append(image)
        if block == wanted_block:
            return decoded
        block += 1
    raise ValueError(f"Bloco {wanted_block} ausente em {path.name}.")


def slayers_decorations(folder):
    with Image.open(folder / "roundbtn.bmp") as source:
        source = source.convert("RGBA")
    key = source.getpixel((0, 0))[:3]
    # roundbtn.bmp holds four 18x51 clasps spaced 64px apart.
    round_button = source.crop((0, 0, 18, 51))
    round_button.putdata([
        pixel if pixel[:3] != key else (0, 0, 0, 0)
        for pixel in round_button.getdata()
    ])

    # menu.cos block 3, frame 2 is the native Slayers minimize button.
    minimize = cos_frames(folder / "menu.cos", 3)[2].crop((14, 7, 34, 33))
    minimize = minimize.resize((10, 13), Image.Resampling.NEAREST)
    return round_button, minimize


def build(panel, header, slayers_img):
    with Image.open(panel) as source:
        source = source.convert("RGB")
    wood = source.crop((13, 464, 465, 480)).resize((452, 34), Image.Resampling.LANCZOS)
    grain = ImageEnhance.Brightness(wood).enhance(0.63)
    panel = Image.new("RGB", (452, 464))
    for y in range(0, panel.height, grain.height):
        plank = grain if (y // grain.height) % 2 == 0 else ImageOps.mirror(grain)
        panel.paste(plank, (0, y))
    header_strip = source.crop((16, 0, 93, 15))
    belt = Image.new("RGB", (854, 16))
    footer = header_strip.resize((header_strip.width, 16), Image.Resampling.NEAREST)
    for x in range(0, belt.width, footer.width):
        belt.paste(footer if (x // footer.width) % 2 == 0 else ImageOps.mirror(footer), (x, 0))
    round_button, minimize = slayers_decorations(slayers_img)
    with header.open("w", encoding="ascii") as stream:
        write_pixels(stream, "hud_wood", panel)
        write_pixels(stream, "portrait_background", source.crop((499, 205, 612, 455)))
        write_pixels(stream, "hud_belt", belt)
        write_pixels(stream, "hud_header", header_strip)
        write_pixels(stream, "hud_divider", source.crop((465, 38, 479, 194)))
        write_keyed_pixels(stream, "hud_round_button", round_button)
        write_pixels(stream, "hud_minimize", minimize.convert("RGB"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("panel", type=Path)
    parser.add_argument("header", type=Path)
    parser.add_argument("slayers_img", type=Path)
    args = parser.parse_args()
    build(args.panel, args.header, args.slayers_img)
