"""Build a DirectDraw compatibility executable for the English Akuma CD."""
import argparse
import hashlib
from pathlib import Path
import struct

import pefile


ORIGINAL_SHA256 = "11f8c6e0aeeaaa19ccd2c7f95693298e5587da60a0d5276a2671960fc4f1a0a7"
IMAGE_BASE = 0x400000
CODE_RVA = 0xD2000
DATA_RVA = 0xD3000
FRAME_COUNTER = IMAGE_BASE + DATA_RVA
CAMERA_COLUMNS = FRAME_COUNTER + 4
CAMERA_ROWS = FRAME_COUNTER + 8
CAMERA_REVEAL_X = FRAME_COUNTER + 12
CAMERA_REVEAL_Y = FRAME_COUNTER + 16
WRAPPER_SHA256 = "85e0f7d530dfda134793a57cb3e76b0287dcc96892ee57162dd68f47283b03a9"
PLAYER_SHA256 = "4b228bdf2dca8d75cbf34d5e1576b3d303e4308fdbf157d20bd311d4ff633908"


def align(n, alignment):
    return (n + alignment - 1) // alignment * alignment


def u32(n):
    return struct.pack("<I", n)


def branch(opcode, address, target):
    return opcode + struct.pack("<i", target - address - len(opcode) - 4)


def add_sections(pe, data, sections):
    file_alignment = pe.OPTIONAL_HEADER.FileAlignment
    section_alignment = pe.OPTIONAL_HEADER.SectionAlignment
    header = pe.sections[-1].get_file_offset() + 40
    if any(data[header:header + len(sections) * 40]) or header + len(sections) * 40 > pe.OPTIONAL_HEADER.SizeOfHeaders:
        raise ValueError("Sem espaco para as novas secoes PE.")
    for name, rva, payload, flags in sections:
        raw_size = align(len(payload), file_alignment)
        raw_at = len(data)
        struct.pack_into("<8sIIIIIIHHI", data, header, name, len(payload), rva,
                         raw_size, raw_at, 0, 0, 0, 0, flags)
        data.extend(payload.ljust(raw_size, b"\0"))
        header += 40

    struct.pack_into("<H", data, pe.FILE_HEADER.get_field_absolute_offset("NumberOfSections"),
                     pe.FILE_HEADER.NumberOfSections + len(sections))
    code_size = sum(align(len(payload), file_alignment) for _, _, payload, flags in sections if flags & 0x20)
    data_size = sum(align(len(payload), file_alignment) for _, _, payload, flags in sections if flags & 0x40)
    end_rva = max(rva + len(payload) for _, rva, payload, _ in sections)
    for field, value in {
        "SizeOfImage": align(end_rva, section_alignment),
        "SizeOfCode": pe.OPTIONAL_HEADER.SizeOfCode + code_size,
        "SizeOfInitializedData": pe.OPTIONAL_HEADER.SizeOfInitializedData + data_size,
        "CheckSum": 0,
    }.items():
        struct.pack_into("<I", data, pe.OPTIONAL_HEADER.get_field_absolute_offset(field), value)


def build(original):
    if hashlib.sha256(original).hexdigest() != ORIGINAL_SHA256:
        raise ValueError("Esta revisao de Akuma.exe nao e suportada.")
    pe = pefile.PE(data=original)
    data = bytearray(original)
    directory = pe.OPTIONAL_HEADER.DATA_DIRECTORY[1]
    imports = pe.get_data(directory.VirtualAddress, directory.Size)
    if imports[-20:] != bytes(20):
        raise ValueError("Tabela de imports sem terminador.")

    # Retain the original imports and attach the compatibility modules in
    # the private PE section. The first DWORD remains the wrapper FPS counter.
    libraries = (
        (b"ddraw.dll", ("DirectDrawCreate",)),
        (b"AkumaHud.dll", ("HudMode@16", "HudAttach@4", "HudFlush@0", "HudWindow@16",
                          "DrawTileOverlay@24", "HudBeginWorld@4", "HudExtendGround@0",
                          "HudExtendForeground@0", "HudEndWorld@0", "HudMapDot",
                          "HudOrbSprite", "HudCursorSprite", "HudCursorBlend", "HudWorldClip@4",
                          "HudSelection", "HudCameraKey@4", "HudQuest@4",
                          "HudDamageParty@24", "HudDamageMob@16", "HudHoverText@20",
                          "HudBeginHeader@0", "HudEndHeader@0", "HudQuickItem@0",
                          "HudDialogue@4", "HudBeginCampaignPaint@0",
                          "HudWorldMap@0", "HudPauseBackdrop@4",
                          "HudWorldMapLabel", "HudWorldMapShadeLabel",
                          "HudNetSend@20", "HudNetRecv@16", "HudChatLines@4", "HudDialogBlt@52")),
    )
    descriptors_offset = 20
    size = len(imports) + len(libraries) * 20
    content = bytearray(struct.pack("<5I", 0, 14, 14, 31, 31) + imports[:-20]
                        + bytes((len(libraries) + 1) * 20))
    functions = {}
    for number, (library, names) in enumerate(libraries):
        filename = DATA_RVA + len(content)
        content.extend(library + b"\0")
        hints = []
        for name in names:
            content.extend(bytes(align(len(content), 2) - len(content)))
            hints.append(DATA_RVA + len(content))
            content.extend(b"\0\0" + name.encode("ascii") + b"\0")
        content.extend(bytes(align(len(content), 4) - len(content)))
        lookup = DATA_RVA + len(content)
        thunks = b"".join(u32(hint) for hint in hints) + bytes(4)
        content.extend(thunks)
        iat = DATA_RVA + len(content)
        content.extend(thunks)
        index = descriptors_offset + len(imports) - 20 + number * 20
        struct.pack_into("<IIIII", content, index, lookup, 0, 0, filename, iat)
        for i, name in enumerate(names):
            functions[name.split("@", 1)[0]] = IMAGE_BASE + iat + i * 4

    paths = (
        (b"img\\lang-en.gos", (0x41C5F9,)),
        (b"img\\icon1-en.gos", (0x41D2AF,)),
        (b"img\\icon10-en.gos", (0x41CFF9,)),
        (b"img\\menuicon-en.gos", (0x41D2D6,)),
        (b"img\\menuicon0-en.gos", (0x41D020,)),
        (b"sam-en.evt", (0x405BE2, 0x406F0A)),
        (b"under-en.evt", (0x4069B4, 0x406F2F)),
        (b"multi1-en.evt", (0x405BC4,)),
        (b"multi2-en.evt", (0x406999,)),
    )
    languages = []
    for path, sites in paths:
        address = IMAGE_BASE + DATA_RVA + len(content)
        languages.append(address + path.index(b"-en.") + 1)
        content.extend(path + b"\0")
        for site in sites:
            offset = pe.get_offset_from_rva(site - IMAGE_BASE)
            data[offset:offset + 4] = u32(address)
    charset = IMAGE_BASE + DATA_RVA + len(content)
    content.append(0)
    empty_string = IMAGE_BASE + DATA_RVA + len(content)
    content.append(0)
    # Font face per language. GDI picks the code page from the font charset, so each
    # CJK language needs its own charset (0x81 Hangul, 0x88 Big5) and a face that has it.
    malgun_face = IMAGE_BASE + DATA_RVA + len(content)
    content.extend(b"Malgun Gothic\0")
    jhenghei_face = IMAGE_BASE + DATA_RVA + len(content)
    content.extend(b"Microsoft JhengHei\0")
    face_slot = align(len(content), 4)
    content.extend(bytes(face_slot + 4 - len(content)))
    face_pointer = IMAGE_BASE + DATA_RVA + face_slot
    font_slot_offset = align(len(content), 4)
    content.extend(bytes(font_slot_offset + 4 - len(content)))

    # WinMain initializes the language table before it opens the main window.
    code = bytearray()
    prepare_at = 0x421316
    setup = IMAGE_BASE + CODE_RVA
    offset = pe.get_offset_from_rva(prepare_at - IMAGE_BASE)
    data[offset:offset + 5] = branch(b"\xe9", prepare_at, setup)
    prepare = bytearray(bytes.fromhex("8b 84 24 d8 00 00 00 50 50 ff 15"))
    prepare.extend(u32(0x489118))  # lstrlenA
    prepare.extend(bytes.fromhex("5a 83 f8 0e 75 00"))
    skip_length = len(prepare) - 1
    # The language is the two letters at "-xxxx-intro XX": ko / zh pick a CJK font, pt
    # only renames the files, anything else (en) keeps the original English names.
    prepare.extend(bytes.fromhex("0f b7 42 0c"))
    to_store = []
    for tag, charset_id, face in ((b"ko", 0x81, malgun_face), (b"zh", 0x88, jhenghei_face)):
        prepare.extend(bytes.fromhex("66 3d") + tag + bytes.fromhex("75 00"))
        next_test = len(prepare) - 1
        prepare.extend(bytes.fromhex("c6 05") + u32(charset) + bytes([charset_id]))
        prepare.extend(bytes.fromhex("c7 05") + u32(face_pointer) + u32(face))
        prepare.extend(bytes.fromhex("eb 00"))
        to_store.append(len(prepare) - 1)
        prepare[next_test] = len(prepare) - next_test - 1
    prepare.extend(bytes.fromhex("66 3d 70 74 75 00"))
    skip_language = len(prepare) - 1
    store = len(prepare)
    for jump in to_store:
        prepare[jump] = store - jump - 1
    for language in languages:
        prepare.extend(bytes.fromhex("66 a3") + u32(language))
    for jump in (skip_length, skip_language):
        prepare[jump] = len(prepare) - jump - 1
    prepare.extend(branch(b"\xe8", setup + len(prepare), 0x41C390))
    prepare.extend(branch(b"\xe9", setup + len(prepare), prepare_at + 5))
    code.extend(prepare)

    # GDI uses the selected font's charset when interpreting ANSI text.
    font_hook = IMAGE_BASE + CODE_RVA + len(code)
    code.extend(bytes.fromhex("a0") + u32(charset))                 # mov al, [charset]
    code.extend(bytes.fromhex("84 c0 74 00"))                       # test al, al / jz original
    skip_font = len(code) - 1
    code.extend(bytes.fromhex("0f b6 c0 89 44 24 24"))              # lfCharSet = charset
    code.extend(bytes.fromhex("a1") + u32(face_pointer))            # mov eax, [face]
    code.extend(bytes.fromhex("89 44 24 38"))                       # lpszFace = face
    code[skip_font] = len(code) - skip_font - 1
    code.extend(bytes.fromhex("ff 25") + u32(0x489018))
    content[font_slot_offset:font_slot_offset + 4] = u32(font_hook)
    text = pe.sections[0]
    text_start = text.PointerToRawData
    text_end = text_start + text.SizeOfRawData
    data[text_start:text_end] = data[text_start:text_end].replace(
        u32(0x489018), u32(IMAGE_BASE + DATA_RVA + font_slot_offset))

    # DirectDrawCreate returns IDirectDraw (v1). Akuma calls IDirectDraw2's
    # five-argument SetDisplayMode, so query that interface and release v1.
    create_at = 0x4010E3
    setup = IMAGE_BASE + CODE_RVA + len(code)
    offset = pe.get_offset_from_rva(create_at - IMAGE_BASE)
    data[offset:offset + 5] = branch(b"\xe9", create_at, setup)

    create = bytearray(bytes.fromhex("83 ec 04 8d 04 24 6a 00 50 6a 00 ff 15"))
    create.extend(u32(functions["DirectDrawCreate"]))
    create.extend(bytes.fromhex("85 c0 78 00"))
    skip_query = len(create) - 1
    create.extend(bytes.fromhex("8b 14 24 8b 02 56 68"))
    create.extend(u32(0x489280))  # Original IID_IDirectDraw2.
    create.extend(bytes.fromhex("52 ff 10 8b 14 24 50 8b 0a 52 ff 51 08 58"))
    create[skip_query] = len(create) - skip_query - 1
    create.extend(bytes.fromhex("83 c4 04 85 c0"))
    create.extend(branch(b"\x0f\x88", setup + len(create), 0x401118))
    create.extend(branch(b"\xe9", setup + len(create), 0x40110A))
    code.extend(create)

    # The original aborts startup if MCI cannot open a CD audio device.
    # Continue without CD music and keep the original sound-effect path.
    no_cd = 0x41C886
    offset = pe.get_offset_from_rva(no_cd - IMAGE_BASE)
    data[offset:offset + 10] = branch(b"\xe9", no_cd, 0x41C8AC) + b"\x90" * 5
    play_cd = 0x47DEF0
    offset = pe.get_offset_from_rva(play_cd - IMAGE_BASE)
    play_guard = IMAGE_BASE + CODE_RVA + len(code)
    data[offset:offset + 5] = branch(b"\xe9", play_cd, play_guard)
    # Track 7 is the first one the game's random music selection can use.
    code.extend(bytes.fromhex("83 79 08 07 73 05 31 c0 c2 08 00 83 ec 18 53 56"))
    code.extend(branch(b"\xe9", IMAGE_BASE + CODE_RVA + len(code), play_cd + 5))

    # Multi Play: the connection dialog (resource 111) seeds its sorted list
    # box with LB_ADDSTRING and a NULL string, which is the blank first row
    # that means "no connection selected". Current user32 measures that string
    # and crashes. Send an empty string instead; the row sorts first as before.
    site = 0x40D5AC
    old = bytes.fromhex("6a00 6a00 6880010000 68f3030000 56 ffd7")
    offset = pe.get_offset_from_rva(site - IMAGE_BASE)
    if data[offset:offset + len(old)] != old:
        raise ValueError("Lista de conexoes do Multi Play inesperada.")
    setup = IMAGE_BASE + CODE_RVA + len(code)
    data[offset:offset + len(old)] = branch(b"\xe9", site, setup).ljust(len(old), b"\x90")
    code.extend(b"\x68" + u32(empty_string) + bytes.fromhex("6a00 6880010000 68f3030000 56 ffd7"))
    code.extend(branch(b"\xe9", IMAGE_BASE + CODE_RVA + len(code), site + len(old)))

    # Network trace hooks (they only log when the HUD enables netlog). The
    # IDirectPlay3A Send wrapper reports its arguments before it runs.
    site = 0x401FE0
    old = bytes.fromhex("8b4908 b805400080")
    offset = pe.get_offset_from_rva(site - IMAGE_BASE)
    if data[offset:offset + len(old)] != old:
        raise ValueError("Funcao Send do DirectPlay inesperada.")
    setup = IMAGE_BASE + CODE_RVA + len(code)
    data[offset:offset + len(old)] = branch(b"\xe9", site, setup).ljust(len(old), b"\x90")
    code.extend(b"\x60" + bytes.fromhex("ff742434") * 5 + b"\xff\x15" + u32(functions["HudNetSend"]) + b"\x61")
    code.extend(old)
    code.extend(branch(b"\xe9", IMAGE_BASE + CODE_RVA + len(code), site + len(old)))

    # The only Receive caller reports each message after the call fills it.
    site = 0x4244FC
    offset = pe.get_offset_from_rva(site - IMAGE_BASE)
    if data[offset:offset + 5] != branch(b"\xe8", site, 0x402010):
        raise ValueError("Chamada de Receive do DirectPlay inesperada.")
    setup = IMAGE_BASE + CODE_RVA + len(code)
    data[offset:offset + 5] = branch(b"\xe8", site, setup)
    code.extend(bytes.fromhex("56 57 53 8b742410 8b7c2414 8b5c2420") + bytes.fromhex("ff742420") * 5)
    code.extend(branch(b"\xe8", IMAGE_BASE + CODE_RVA + len(code), 0x402010))
    code.extend(bytes.fromhex("85c0 78 00"))
    skip_report = len(code) - 1
    code.extend(bytes.fromhex("50 ff33 ff742424 ff37 ff36 ff15") + u32(functions["HudNetRecv"]) + b"\x58")
    code[skip_report] = len(code) - skip_report - 1
    code.extend(bytes.fromhex("5b 5f 5e c2 1400"))

    # The chat object's draw method writes the message lines in a fixed corner with a
    # small font, which shrinks with the zoom. Let the HUD draw them in multiplayer.
    site = 0x489794
    offset = pe.get_offset_from_rva(site - IMAGE_BASE)
    if data[offset:offset + 4] != u32(0x423940):
        raise ValueError("Desenho das linhas do chat inesperado.")
    setup = IMAGE_BASE + CODE_RVA + len(code)
    code.extend(bytes.fromhex("58 51 50 ff 25") + u32(functions["HudChatLines"]))
    data[offset:offset + 4] = u32(setup)

    # The multiplayer dialogs draw their background with this StretchDIBits call.
    site = 0x46B3EB
    offset = pe.get_offset_from_rva(site - IMAGE_BASE)
    if data[offset:offset + 6] != bytes.fromhex("ff15") + u32(0x489048):
        raise ValueError("Fundo dos dialogos do multiplayer inesperado.")
    data[offset + 2:offset + 6] = u32(functions["HudDialogBlt"])

    # Keep the AVI assets and normal ending. Skip the three startup clips
    # only when the launcher supplies the skip option.
    intro_at = 0x421346
    offset = pe.get_offset_from_rva(intro_at - IMAGE_BASE)
    code.extend(b"-skip-intro ")
    argument = IMAGE_BASE + CODE_RVA + len(code) - len(b"-skip-intro ")
    check_intro = IMAGE_BASE + CODE_RVA + len(code)
    data[offset:offset + 5] = branch(b"\xe9", intro_at, check_intro)
    code.extend(bytes.fromhex("8b b4 24 ec 00 00 00 bf") + u32(argument))
    code.extend(bytes.fromhex("b9 0c 00 00 00 f3 a6 74 0a b9 11 00 00 00"))
    code.extend(branch(b"\xe9", IMAGE_BASE + CODE_RVA + len(code), intro_at + 5))
    code.extend(bytes.fromhex("83 c4 04"))
    code.extend(branch(b"\xe9", IMAGE_BASE + CODE_RVA + len(code), 0x42145F))

    # Keep the original 640x480 offscreen drawing surface. HudMode can
    # request a 16:9 primary and disable flipping before surface creation.
    site = 0x4012C7
    offset = pe.get_offset_from_rva(site - IMAGE_BASE)
    setup = IMAGE_BASE + CODE_RVA + len(code)
    data[offset:offset + 5] = branch(b"\xe9", site, setup)
    code.extend(bytes.fromhex("55 53 57 56 ff 15") + u32(functions["HudMode"]))
    code.extend(branch(b"\xe9", IMAGE_BASE + CODE_RVA + len(code), 0x4012D7))

    # Create the engine's virtual primary after the original 640x480 back
    # surface and the real 16:9 primary are ready.
    site = 0x4012B2
    offset = pe.get_offset_from_rva(site - IMAGE_BASE)
    setup = IMAGE_BASE + CODE_RVA + len(code)
    data[offset:offset + 5] = branch(b"\xe9", site, setup)
    code.extend(branch(b"\xe8", setup, 0x4012F0))
    code.extend(bytes.fromhex("85 c0 74 07 56 ff 15") + u32(functions["HudAttach"]))
    code.extend(branch(b"\xe9", IMAGE_BASE + CODE_RVA + len(code), site + 5))

    # Preserve the original present, then publish its complete virtual
    # primary. This also presents the loading screens before the game loop.
    site = 0x401570
    offset = pe.get_offset_from_rva(site - IMAGE_BASE)
    setup = IMAGE_BASE + CODE_RVA + len(code)
    data[offset:offset + 8] = branch(b"\xe9", site, setup) + b"\x90" * 3
    code.extend(branch(b"\xe8", setup, setup + 14))
    code.extend(bytes.fromhex("50 ff 15") + u32(functions["HudFlush"]) + bytes.fromhex("58 c3"))
    code.extend(bytes.fromhex("56 8b f1"))
    code.extend(branch(b"\xe8", IMAGE_BASE + CODE_RVA + len(code), 0x4018B0))
    code.extend(branch(b"\xe9", IMAGE_BASE + CODE_RVA + len(code), 0x401578))

    # The gameplay loop also draws partial updates straight into its
    # primary. Present them after the state update has completed.
    site = 0x421549
    offset = pe.get_offset_from_rva(site - IMAGE_BASE)
    setup = IMAGE_BASE + CODE_RVA + len(code)
    data[offset:offset + 5] = branch(b"\xe9", site, setup)
    code.extend(bytes.fromhex("ff 52 04 50 ff 15") + u32(functions["HudFlush"]) + bytes.fromhex("58 85 c0"))
    code.extend(branch(b"\xe9", IMAGE_BASE + CODE_RVA + len(code), site + 5))

    # Register the forwarding WndProc before cnc-ddraw subclasses it. Its
    # input coordinates then originate from the wrapper's logical surface.
    setup = IMAGE_BASE + CODE_RVA + len(code)
    offset = pe.get_offset_from_rva(0x41C513 - IMAGE_BASE)
    data[offset:offset + 4] = u32(setup)
    code.extend(bytes.fromhex("ff 25") + u32(functions["HudWindow"]))

    # The campaign draws ground, actors, foreground tiles, remaining actors, and
    # then the interface. Extend the terrain at its original two layer boundaries.
    for site, original_call, hook, receives_map in (
        (0x467524, 0x45A520, "HudBeginWorld", True),
        (0x467539, 0x4017C0, "HudExtendGround", False),
        (0x467589, 0x4017C0, "HudExtendForeground", False),
    ):
        offset = pe.get_offset_from_rva(site - IMAGE_BASE)
        if data[offset:offset + 5] != branch(b"\xe8", site, original_call):
            raise ValueError("Ordem de desenho da campanha inesperada.")
        setup = IMAGE_BASE + CODE_RVA + len(code)
        data[offset:offset + 5] = branch(b"\xe8", site, setup)
        code.extend(b"\x51")
        if receives_map:
            code.extend(b"\x51")
        code.extend(bytes.fromhex("ff 15") + u32(functions[hook]) + b"\x59")
        code.extend(branch(b"\xe9", IMAGE_BASE + CODE_RVA + len(code), original_call))

    # Unlock after the last actor draw, then save the extended strip before
    # the native minimap at 0x4675BB paints into those same backbuffer pixels.
    site = 0x4675AE
    offset = pe.get_offset_from_rva(site - IMAGE_BASE)
    if data[offset:offset + 5] != branch(b"\xe8", site, 0x401810):
        raise ValueError("Fim do desenho do mundo inesperado.")
    setup = IMAGE_BASE + CODE_RVA + len(code)
    data[offset:offset + 5] = branch(b"\xe8", site, setup)
    code.extend(branch(b"\xe8", setup, 0x401810))
    code.extend(bytes.fromhex("50 ff 15") + u32(functions["HudEndWorld"]) + bytes.fromhex("58 c3"))

    # Draw the map caption and quick slots onto their projected background
    # between the fog pass and the final unlock of the top toolbar.
    site, title = 0x4675C4, 0x45D3A0
    offset = pe.get_offset_from_rva(site - IMAGE_BASE)
    if data[offset:offset + 5] != branch(b"\xe8", site, title):
        raise ValueError("Desenho do titulo do mapa inesperado.")
    setup = IMAGE_BASE + CODE_RVA + len(code)
    data[offset:offset + 5] = branch(b"\xe8", site, setup)
    code.extend(bytes.fromhex("51 ff15") + u32(functions["HudBeginHeader"]) + b"\x59")
    code.extend(branch(b"\xe9", IMAGE_BASE + CODE_RVA + len(code), title))

    site, unlock = 0x4676B8, 0x401810
    offset = pe.get_offset_from_rva(site - IMAGE_BASE)
    if data[offset:offset + 5] != branch(b"\xe8", site, unlock):
        raise ValueError("Fim do desenho dos atalhos inesperado.")
    setup = IMAGE_BASE + CODE_RVA + len(code)
    data[offset:offset + 5] = branch(b"\xe8", site, setup)
    code.extend(branch(b"\xe8", setup, unlock))
    code.extend(bytes.fromhex("50 ff15") + u32(functions["HudEndHeader"]) + bytes.fromhex("58 c3"))

    # Enter the inventory's item-description block with the hovered quick item.
    site = 0x46771A
    old = bytes.fromhex("8b86a400000085c0") + branch(b"\x0f\x84", site + 8, 0x4695C1)
    offset = pe.get_offset_from_rva(site - IMAGE_BASE)
    if data[offset:offset + len(old)] != old:
        raise ValueError("Selecao de descricao da campanha inesperada.")
    setup = IMAGE_BASE + CODE_RVA + len(code)
    data[offset:offset + len(old)] = branch(b"\xe9", site, setup).ljust(len(old), b"\x90")
    code.extend(bytes.fromhex("ff15") + u32(functions["HudQuickItem"]) + bytes.fromhex("85c0740b"))
    code.extend(bytes.fromhex("8bf8897c241c"))
    code.extend(branch(b"\xe9", IMAGE_BASE + CODE_RVA + len(code), 0x4689D1))
    code.extend(old[:8])
    code.extend(branch(b"\x0f\x84", IMAGE_BASE + CODE_RVA + len(code), 0x4695C1))
    code.extend(branch(b"\xe9", IMAGE_BASE + CODE_RVA + len(code), site + len(old)))

    site = 0x469646
    offset = pe.get_offset_from_rva(site - IMAGE_BASE)
    if data[offset:offset + 5] != branch(b"\xe8", site, 0x47D470):
        raise ValueError("Desenho do diario de missoes inesperado.")
    setup = IMAGE_BASE + CODE_RVA + len(code)
    data[offset:offset + 5] = branch(b"\xe8", site, setup)
    code.extend(bytes.fromhex("51 ff 15") + u32(functions["HudQuest"]) + b"\xc3")

    site = 0x48A990
    offset = pe.get_offset_from_rva(site - IMAGE_BASE)
    if data[offset:offset + 4] != u32(0x47B410):
        raise ValueError("Desenho nativo das conversas inesperado.")
    data[offset:offset + 4] = u32(IMAGE_BASE + CODE_RVA + len(code))
    code.extend(bytes.fromhex("51 ff15") + u32(functions["HudDialogue"]) + b"\xc3")

    # Begin each campaign draw before the world, character or map branch.
    site = 0x4674E0
    offset = pe.get_offset_from_rva(site - IMAGE_BASE)
    old = bytes.fromhex("81ecb0000000")
    if data[offset:offset + len(old)] != old:
        raise ValueError("Inicio do desenho da campanha inesperado.")
    setup = IMAGE_BASE + CODE_RVA + len(code)
    data[offset:offset + len(old)] = branch(b"\xe9", site, setup) + b"\x90"
    code.extend(bytes.fromhex("51ff15") + u32(functions["HudBeginCampaignPaint"]) + b"\x59" + old)
    code.extend(branch(b"\xe9", IMAGE_BASE + CODE_RVA + len(code), site + len(old)))

    # Replace the cropped world-map pass only when the full composition succeeds.
    site = 0x469457
    offset = pe.get_offset_from_rva(site - IMAGE_BASE)
    old = bytes.fromhex("8b96d8000000")
    if data[offset:offset + len(old)] != old:
        raise ValueError("Recorte nativo do mapa-mundi inesperado.")
    setup = IMAGE_BASE + CODE_RVA + len(code)
    data[offset:offset + len(old)] = branch(b"\xe9", site, setup) + b"\x90"
    code.extend(bytes.fromhex("ff15") + u32(functions["HudWorldMap"]) + bytes.fromhex("85c0"))
    code.extend(branch(b"\x0f\x85", IMAGE_BASE + CODE_RVA + len(code), 0x4695C1))
    code.extend(old)
    code.extend(branch(b"\xe9", IMAGE_BASE + CODE_RVA + len(code), site + len(old)))

    for site, original, hook in (
        (0x45D6DF, 0x419F10, "HudWorldMapLabel"),
        (0x45D73E, 0x41A560, "HudWorldMapShadeLabel"),
    ):
        offset = pe.get_offset_from_rva(site - IMAGE_BASE)
        if data[offset:offset + 5] != branch(b"\xe8", site, original):
            raise ValueError("Rotulo nativo do mapa-mundi inesperado.")
        setup = IMAGE_BASE + CODE_RVA + len(code)
        data[offset:offset + 5] = branch(b"\xe8", site, setup)
        code.extend(bytes.fromhex("ff25") + u32(functions[hook]))

    # Keep the native pause labels, hitboxes and submenu handling.
    site = 0x409840
    offset = pe.get_offset_from_rva(site - IMAGE_BASE)
    old = bytes.fromhex("ff511c8a4530")
    if data[offset:offset + len(old)] != old:
        raise ValueError("Fundo nativo da pausa inesperado.")
    setup = IMAGE_BASE + CODE_RVA + len(code)
    data[offset:offset + len(old)] = branch(b"\xe9", site, setup) + b"\x90"
    code.extend(old[:3] + bytes.fromhex("5055ff15") + u32(functions["HudPauseBackdrop"]) + b"\x58" + old[3:])
    code.extend(branch(b"\xe9", IMAGE_BASE + CODE_RVA + len(code), site + len(old)))

    # The campaign and orb toolbar paint their hints independently into
    # the same part of the backbuffer. Give the widescreen HUD one owner.
    site = 0x4674EE
    offset = pe.get_offset_from_rva(site - IMAGE_BASE)
    if data[offset:offset + 6] != b"\x8b\x1d" + u32(0x489030):
        raise ValueError("Rotina nativa de texto da campanha inesperada.")
    setup = IMAGE_BASE + CODE_RVA + len(code)
    data[offset:offset + 6] = b"\xbb" + u32(setup) + b"\x90"
    code.extend(bytes.fromhex("ff25") + u32(functions["HudHoverText"]))
    site = 0x46AC2C
    offset = pe.get_offset_from_rva(site - IMAGE_BASE)
    if data[offset:offset + 6] != b"\xff\x15" + u32(0x489030):
        raise ValueError("Rotina nativa de texto dos atalhos inesperada.")
    data[offset + 2:offset + 6] = u32(functions["HudHoverText"])
    site = 0x4696F7
    offset = pe.get_offset_from_rva(site - IMAGE_BASE)
    if data[offset:offset + 6] != b"\xff\x15" + u32(0x489030):
        raise ValueError("Rotina nativa de atributos do item inesperada.")
    data[offset + 2:offset + 6] = u32(functions["HudHoverText"])

    # The engine already draws these bars. Replace the hover test with
    # current/max health checks and leave the original drawing intact.
    for site, old_hex, fields, hidden in (
        (0x425348, "8b462c85c00f84cf000000", (0x810, 0x7E0), 0x425422),
        (0x426458, "8b462c85c00f84cf000000", (0x810, 0x7E0), 0x426532),
        (0x435138, "8b462c85c00f84cf000000", (0x810, 0x7E0), 0x435212),
        (0x437C3B, "8b462c85c00f8400010000", (0x300, 0x2FC), 0x437D3F),
        (0x43C605, "8b462c85c00f84f0000000", (0x300, 0x2FC), 0x43C6F9),
    ):
        old = bytes.fromhex(old_hex)
        offset = pe.get_offset_from_rva(site - IMAGE_BASE)
        if data[offset:offset + len(old)] != old:
            raise ValueError("Barra de vida nativa inesperada.")
        setup = IMAGE_BASE + CODE_RVA + len(code)
        data[offset:offset + len(old)] = branch(b"\xe9", site, setup).ljust(len(old), b"\x90")
        for field in fields:
            code.extend(bytes.fromhex("83 be") + u32(field) + b"\0")
            code.extend(branch(b"\x0f\x8e", IMAGE_BASE + CODE_RVA + len(code), hidden))
        code.extend(branch(b"\xe9", IMAGE_BASE + CODE_RVA + len(code), site + len(old)))

    # Adapt thiscall to the HUD's stdcall functions without changing the
    # original virtual method's return address or argument order.
    for hook, target, entries in (
        ("HudDamageParty", 0x42B390, (0x4897F8, 0x4898F8, 0x48995C, 0x489A94)),
        ("HudDamageMob", 0x43E620, (0x489AB0, 0x48A38C, 0x48A3A8, 0x48A3C4, 0x48A3E0, 0x48A3FC)),
    ):
        setup = IMAGE_BASE + CODE_RVA + len(code)
        code.extend(bytes.fromhex("58 51 50 ff 25") + u32(functions[hook]))
        for site in entries:
            offset = pe.get_offset_from_rva(site - IMAGE_BASE)
            if data[offset:offset + 4] != u32(target):
                raise ValueError("Rotina nativa de dano inesperada.")
            data[offset:offset + 4] = u32(setup)

    # The actor marker routine draws 2x2 dots into the native minimap slot
    # while the world still uses that part of the backbuffer.
    setup = IMAGE_BASE + CODE_RVA + len(code)
    code.extend(bytes.fromhex("ff 25") + u32(functions["HudMapDot"]))
    for site in (0x458F59, 0x458F63):
        offset = pe.get_offset_from_rva(site - IMAGE_BASE)
        if data[offset:offset + 5] != branch(b"\xe8", site, 0x41B110):
            raise ValueError("Marcador do minimapa inesperado.")
        data[offset:offset + 5] = branch(b"\xe8", site, setup)

    # Keep isolated copies of the five live orb sprites and of the pointer.
    # Native callers retain their original calling convention and cleanup.
    for hook, target, sites in (
        ("HudOrbSprite", 0x41A980, (0x46AA25, 0x46AA5F, 0x46AA95, 0x46AACC, 0x46AB06)),
        ("HudCursorSprite", 0x41A980, (0x47E945,)),
        ("HudCursorBlend", 0x41AA60, (0x47E8F4,)),
        ("HudSelection", 0x41B1B0, (0x4676E5,)),
    ):
        setup = IMAGE_BASE + CODE_RVA + len(code)
        code.extend(bytes.fromhex("ff 25") + u32(functions[hook]))
        for site in sites:
            offset = pe.get_offset_from_rva(site - IMAGE_BASE)
            if data[offset:offset + 5] != branch(b"\xe8", site, target):
                raise ValueError("Desenho nativo da interface inesperado.")
            data[offset:offset + 5] = branch(b"\xe8", site, setup)

    site = 0x466015
    offset = pe.get_offset_from_rva(site - IMAGE_BASE)
    if data[offset:offset + 6] != bytes.fromhex("ff 15") + u32(0x4891C4):
        raise ValueError("Captura nativa do ponteiro inesperada.")
    data[offset:offset + 6] = bytes.fromhex("ff 15") + u32(functions["HudWorldClip"])

    # Only the live campaign camera polls the four keys through this pointer.
    site = 0x466790
    offset = pe.get_offset_from_rva(site - IMAGE_BASE)
    if data[offset:offset + 6] != bytes.fromhex("8b3dc0914800"):
        raise ValueError("Leitura das setas da camera inesperada.")
    setup = IMAGE_BASE + CODE_RVA + len(code)
    data[offset:offset + 6] = b"\xbf" + u32(setup) + b"\x90"
    code.extend(bytes.fromhex("ff 25") + u32(functions["HudCameraKey"]))

    # Keep the yellow map-exit marker present on every expanded frame.
    # Other native markers and the classic presentation keep their timing.
    site = 0x4508A6
    offset = pe.get_offset_from_rva(site - IMAGE_BASE)
    old = bytes.fromhex("a803578bf1752d")
    if data[offset:offset + len(old)] != old:
        raise ValueError("Marcador amarelo da saida inesperado.")
    setup = IMAGE_BASE + CODE_RVA + len(code)
    data[offset:offset + len(old)] = branch(b"\xe9", site, setup) + b"\x90\x90"
    code.extend(bytes.fromhex("57 8b f1 83 3d") + u32(CAMERA_COLUMNS) + b"\x0e")
    code.extend(branch(b"\x0f\x8f", IMAGE_BASE + CODE_RVA + len(code), 0x4508AD))
    code.extend(b"\xa8\x03")
    code.extend(branch(b"\x0f\x85", IMAGE_BASE + CODE_RVA + len(code), 0x4508DA))
    code.extend(branch(b"\xe9", IMAGE_BASE + CODE_RVA + len(code), 0x4508AD))

    # The minimap's discovery grid has two samples per tile. Reveal the
    # same expanded rectangle the camera actually displays in both map modes.
    for site, reg, limit, loop in (
        (0x45A574, 0, CAMERA_REVEAL_X, 0x45A54B),
        (0x45A57A, 6, CAMERA_REVEAL_Y, 0x45A535),
        (0x45A658, 0, CAMERA_REVEAL_X, 0x45A614),
        (0x45A65E, 2, CAMERA_REVEAL_Y, 0x45A5FE),
    ):
        offset = pe.get_offset_from_rva(site - IMAGE_BASE)
        old = bytes((0x83, 0xF8 + reg, 0x1F, 0x7C, (loop - site - 5) & 0xFF))
        if data[offset:offset + len(old)] != old:
            raise ValueError("Limite de descoberta do mapa inesperado.")
        setup = IMAGE_BASE + CODE_RVA + len(code)
        data[offset:offset + 5] = branch(b"\xe9", site, setup)
        code.extend(bytes((0x3B, 0x05 + reg * 8)) + u32(limit))
        code.extend(branch(b"\x0f\x8c", IMAGE_BASE + CODE_RVA + len(code), loop))
        code.extend(branch(b"\xe9", IMAGE_BASE + CODE_RVA + len(code), site + 5))

    # Add one terrain tile to the party's light radius in the widescreen view.
    # The native radius is measured in half-tiles and other lights keep theirs.
    site, reveal_party = 0x45A6A1, 0x459E00
    offset = pe.get_offset_from_rva(site - IMAGE_BASE)
    if data[offset:offset + 5] != branch(b"\xe8", site, reveal_party):
        raise ValueError("Raio de visao do grupo inesperado.")
    setup = IMAGE_BASE + CODE_RVA + len(code)
    data[offset:offset + 5] = branch(b"\xe8", site, setup)
    code.extend(bytes.fromhex("83 3d") + u32(CAMERA_COLUMNS) + b"\x0e")
    code.extend(bytes.fromhex("7e 05 83 44 24 0c 02"))
    code.extend(branch(b"\xe9", IMAGE_BASE + CODE_RVA + len(code), reveal_party))

    # Apply the same limit to edge scrolling and minimap clicks. A small map
    # has a maximum camera position of zero, not a negative tile index.
    for site, dimension, target, original in (
        (0x457626, CAMERA_COLUMNS, 0, "83c0f23bd0"),
        (0x457661, CAMERA_ROWS, 0, "83c0f23bd0"),
        (0x4590B5, CAMERA_COLUMNS, 2, "83c2f23bc2"),
        (0x4590F2, CAMERA_ROWS, 2, "83c2f23bc2"),
    ):
        old = bytes.fromhex(original)
        offset = pe.get_offset_from_rva(site - IMAGE_BASE)
        if data[offset:offset + 5] != old:
            raise ValueError("Limite nativo da camera inesperado.")
        setup = IMAGE_BASE + CODE_RVA + len(code)
        data[offset:offset + 5] = branch(b"\xe9", site, setup)
        code.extend(bytes((0x2B, 0x05 + target * 8)) + u32(dimension))
        code.extend(bytes((0x79, 0x02, 0x31, 0xC0 + target * 9)))
        code.extend(old[3:])
        code.extend(branch(b"\xe9", IMAGE_BASE + CODE_RVA + len(code), site + 5))

    # The native white camera box is drawn in both campaign minimap modes.
    for site, dimension, following, bottom_add in (
        (0x45A7C9, CAMERA_COLUMNS, "8d540823", ""),
        (0x45A7F2, CAMERA_COLUMNS, "8d540830", "0315"),
        (0x45A81B, CAMERA_ROWS, "8d540823", ""),
        (0x45A844, CAMERA_ROWS, "8d540823", ""),
        (0x45A903, CAMERA_COLUMNS, "8d441123", ""),
        (0x45A930, CAMERA_COLUMNS, "8d4c0230", "030d"),
        (0x45A95D, CAMERA_ROWS, "8d540823", ""),
        (0x45A98A, CAMERA_ROWS, "8d441123", ""),
    ):
        resume = bytes.fromhex(following)
        old = b"\x6a\x0e" + resume
        offset = pe.get_offset_from_rva(site - IMAGE_BASE)
        if data[offset:offset + len(old)] != old:
            raise ValueError("Contorno do minimapa inesperado.")
        setup = IMAGE_BASE + CODE_RVA + len(code)
        data[offset:offset + len(old)] = branch(b"\xe9", site, setup) + b"\x90"
        code.extend(bytes.fromhex("ff 35") + u32(dimension))
        if bottom_add:
            code.extend(resume[:-1] + b"\x22" + bytes.fromhex(bottom_add) + u32(CAMERA_ROWS))
        else:
            code.extend(resume)
        code.extend(branch(b"\xe9", IMAGE_BASE + CODE_RVA + len(code), site + len(old)))

    for site, lea, add in (
        (0x45A854, "8d9408", "0315"),
        (0x45A99A, "8d8411", "0305"),
    ):
        calculate = bytes.fromhex(lea)
        old = calculate + u32(0x1F5)
        offset = pe.get_offset_from_rva(site - IMAGE_BASE)
        if data[offset:offset + len(old)] != old:
            raise ValueError("Linha direita do minimapa inesperada.")
        setup = IMAGE_BASE + CODE_RVA + len(code)
        data[offset:offset + len(old)] = branch(b"\xe9", site, setup) + b"\x90\x90"
        code.extend(calculate + u32(487) + bytes.fromhex(add) + u32(CAMERA_COLUMNS))
        code.extend(branch(b"\xe9", IMAGE_BASE + CODE_RVA + len(code), site + len(old)))

    # Preserve the zero-pixel mask at the six terrain-overlay blits.
    # Keep the native base-layer copies and tile order.
    # Start at the argument pushes. The next instruction after BltFast
    # is also a branch target when the cell needs no overlay.
    for site, pushes in (
        (0x4577D0, "51525056"),
        (0x45B94A, "51575253"),
        (0x45BA98, "51575253"),
        (0x45BC17, "51525357"),
        (0x45BD6B, "51525357"),
        (0x45BF9A, "51565357"),
    ):
        arguments = bytes.fromhex(pushes)
        old = arguments + b"\xff\x55\x1c"
        offset = pe.get_offset_from_rva(site - IMAGE_BASE)
        if data[offset:offset + len(old)] != old:
            raise ValueError("Copia de tile da segunda camada inesperada.")
        setup = IMAGE_BASE + CODE_RVA + len(code)
        data[offset:offset + len(old)] = branch(b"\xe9", site, setup) + b"\x90" * (len(old) - 5)
        code.extend(arguments + bytes.fromhex("ff 15") + u32(functions["DrawTileOverlay"]))
        code.extend(branch(b"\xe9", IMAGE_BASE + CODE_RVA + len(code), site + len(old)))

    add_sections(pe, data, [
        (b".akcode", CODE_RVA, code, 0x60000020),
        (b".akdata", DATA_RVA, content, 0xC0000040),
    ])
    struct.pack_into("<II", data, directory.get_file_offset(), DATA_RVA + descriptors_offset, size)
    return data


def patch_wrapper(source, output):
    original = source.read_bytes()
    if hashlib.sha256(original).hexdigest() != WRAPPER_SHA256:
        raise ValueError("Use a DLL x86 de cnc-ddraw v7.1.0.0.")
    pe = pefile.PE(data=original)
    data = bytearray(original)
    frame_end = 0x6AE0
    hook = pe.OPTIONAL_HEADER.SizeOfImage
    # Count the render-loop iteration immediately before the wrapper waits
    # for the next frame. Each backend has one call to fpsl_frame_end.
    for site in (0x1BB6E, 0x1D18E, 0x1D694):
        offset = pe.get_offset_from_rva(site)
        if data[offset:offset + 5] != branch(b"\xe8", site, frame_end):
            raise ValueError("Loop de renderizacao do cnc-ddraw inesperado.")
        data[offset:offset + 5] = branch(b"\xe8", site, hook)
    # The game EXE has a fixed base, while the DLL can still use ASLR.
    increment = b"\xff\x05" + u32(FRAME_COUNTER)
    stub = increment + branch(b"\xe9", hook + len(increment), frame_end)
    add_sections(pe, data, [(b".akfps", hook, stub, 0x60000020)])
    with output.open("xb") as file:
        file.write(data)



def patch_player(player):
    original = player.read_bytes()
    if hashlib.sha256(original).hexdigest() != PLAYER_SHA256:
        raise ValueError("Esta revisao de MCIPLAY.EXE nao e suportada.")
    old = b"%sAkuma\\Img\\%s\0"
    # Suppress the drive string and resolve the AVI inside the extracted folder.
    new = b"%.0sImg\\%s\0".ljust(len(old), b"\0")
    if original.count(old) != 1:
        raise ValueError("Caminho dos videos nao localizado em MCIPLAY.EXE.")
    player.write_bytes(original.replace(old, new))



if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--player", type=Path)
    parser.add_argument("--wrapper", type=Path, nargs=2, metavar=("SOURCE", "OUTPUT"))
    args = parser.parse_args()
    with args.output.open("xb") as output:
        output.write(build(args.original.read_bytes()))
    if args.player is not None:
        patch_player(args.player)
    if args.wrapper is not None:
        patch_wrapper(*args.wrapper)







