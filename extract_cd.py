"""Extract the playable data directory from the supplied MODE2/2352 CD."""
import argparse
from pathlib import Path
import struct

RAW_SECTOR = 2352
DATA_START = 24
DATA_SIZE = 2048
BATCH_SIZE = 256
SYNC = b"\x00" + b"\xff" * 10 + b"\x00"


class Disc:
    def __init__(self, image):
        self.image = image
        self.encoding = "ascii"

    def chunks(self, first, size):
        self.image.seek(first * RAW_SECTOR)
        while size:
            count = min(BATCH_SIZE, (size + DATA_SIZE - 1) // DATA_SIZE)
            raw = self.image.read(count * RAW_SECTOR)
            if len(raw) != count * RAW_SECTOR:
                raise ValueError("Imagem de CD truncada.")
            data = bytearray()
            for offset in range(0, len(raw), RAW_SECTOR):
                if (raw[offset:offset + 12] != SYNC or raw[offset + 15] != 2
                        or raw[offset + 18] & 0x20):
                    raise ValueError("Setor fora do formato MODE2 Form 1.")
                data.extend(raw[offset + DATA_START:offset + DATA_START + DATA_SIZE])
            yield data[:size]
            size -= min(size, len(data))

    @staticmethod
    def extent(record):
        return struct.unpack_from("<I4xI", record, 2)

    def entries(self, first, size):
        offset = 0
        data = b"".join(self.chunks(first, size))
        while offset < len(data):
            length = data[offset]
            if not length:
                offset += DATA_SIZE - (offset % DATA_SIZE)
                continue
            record = data[offset:offset + length]
            offset += length
            if len(record) < 34 or record[32] > len(record) - 33 or record[25] & 128:
                raise ValueError("Entrada ISO 9660 invalida ou dividida em varios extents.")
            identifier = record[33:33 + record[32]]
            if identifier in (b"\0", b"\1"):
                continue
            name = identifier.decode(self.encoding).split(";", 1)[0]
            if name in ("", ".", "..") or any(c in name for c in '/\\:'):
                raise ValueError("Nome ISO 9660 invalido: " + name)
            yield name, record[25] & 2, self.extent(record)

    def extract(self, directory, target):
        target.mkdir()
        for name, is_dir, (first, size) in self.entries(*directory):
            path = target / name
            if is_dir:
                self.extract((first, size), path)
            else:
                with path.open("xb") as output:
                    for data in self.chunks(first, size):
                        output.write(data)

    def extract_game(self, target):
        # Joliet preserves the actual asset names. The primary directory
        # replaces names such as gearbig-d.gos with DOS-compatible aliases.
        for sector in range(16, 48):
            descriptor = b"".join(self.chunks(sector, DATA_SIZE))
            if descriptor[1:7] != b"CD001\x01" or descriptor[0] == 255:
                break
            if descriptor[0] == 2 and descriptor[88:91] in (b"%/@", b"%/C", b"%/E"):
                self.encoding = "utf-16-be"
                break
        else:
            raise ValueError("Sequencia de descritores ISO 9660 longa demais.")
        if self.encoding != "utf-16-be":
            raise ValueError("Diretorio Joliet ausente. Use o BIN do CD original.")
        entries = self.entries(*self.extent(descriptor[156:190]))
        for name, is_dir, extent in entries:
            if name.casefold() == "akuma" and is_dir:
                self.extract(extent, target)
                return
        raise ValueError("Pasta Akuma ausente no CD.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("target", type=Path)
    args = parser.parse_args()
    with args.image.open("rb") as image:
        Disc(image).extract_game(args.target)

