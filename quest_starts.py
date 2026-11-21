"""Index quest starts from the campaign scripts for the HUD."""

from pathlib import Path
import re
import sys


EVENT = re.compile(
    rb'^[ \t]*#[ \t]*(\d+)[ \t]*\r?\n'
    rb'#[ \t]*(\d+)[ \t]*\r?\n'
    rb'#[ \t]*(\d+)[ \t]*\r?\n'
    rb'#[ \t]*"([^"\r\n]*)"[ \t]*\r?\n'
    rb'#[ \t]*(BASIC|FLOW)\b',
    re.M | re.I,
)
ORDER = re.compile(rb'^[ \t]*@[ \t]*ORDER[ \t]+(\d+)\b', re.M | re.I)


def starts(path):
    source = path.read_bytes()
    events = list(EVENT.finditer(source))
    for index, event in enumerate(events):
        end = events[index + 1].start() if index + 1 < len(events) else len(source)
        body = source[event.end():end]
        for order in ORDER.findall(body):
            yield int(event[1]), int(order)


if __name__ == "__main__":
    folder, header = map(Path, sys.argv[1:])
    with header.open("w", encoding="ascii") as out:
        print("static const struct { unsigned char surface; unsigned short event, order; } quest_starts[] = {", file=out)
        for surface, name in enumerate(("under-en.evt", "sam-en.evt")):
            for event, order in sorted(set(starts(folder / name))):
                print(f"    {{{surface}, {event}, {order}}},", file=out)
        print("};", file=out)
