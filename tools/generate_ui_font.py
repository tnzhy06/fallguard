"""Render complete, metric-aware UI bitmaps from the official HarmonyOS Sans SC TTFs.

python tools/generate_ui_font.py --font-dir build/font-assets/HarmonyOS_Sans_SC
Source: https://developer.huawei.com/consumer/cn/design/resource-V1/
"""
import argparse
from functools import lru_cache
import hashlib
import math
from pathlib import Path
import re

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
SCALE, PAD, HPAD, GUARD = 4, 2, 1, 8
STRING = r'"(?:\\.|[^"\\])*"'


def calls(source, name):
    """Read UI helper arguments, preserving nested calls and string commas."""
    for match in re.finditer(rf"\b{re.escape(name)}\s*\(", source):
        depth, start, args = 1, match.end(), []
        begin = start
        for token in re.finditer(STRING + r"|[(),]", source[start:]):
            value, pos = token.group(), start + token.start()
            if value == "(":
                depth += 1
            elif value == ")":
                depth -= 1
                if not depth:
                    args.append(source[begin:pos].strip())
                    yield args
                    break
            elif value == "," and depth == 1:
                args.append(source[begin:pos].strip())
                begin = pos + 1


def font_characters(ui_source, boot_source):
    """Keep all small/log text, and only used Chinese at the larger sizes.

    ASCII remains available at each size for formatted values and identifiers.
    Text providers (e.g. fall_status) are followed so every status is included.
    """
    ascii_chars = set(map(chr, range(32, 127)))
    all_text = "".join(re.findall(STRING, ui_source + boot_source))
    chinese = set(c for c in all_text if "\u4e00" <= c <= "\u9fff")
    used = {14: ascii_chars | chinese, 16: set(ascii_chars), 20: set(ascii_chars)}

    def collect(expression, seen=None):
        seen = set() if seen is None else seen
        literals = "".join(re.findall(STRING, expression))
        for name in re.findall(r"\b(\w+)\s*\(", re.sub(STRING, "", expression)):
            if name in seen:
                continue
            seen.add(name)
            definition = re.search(rf"\b{re.escape(name)}\s*\([^;{{}}]*\)\s*\{{", ui_source)
            if not definition:
                continue
            depth, begin, end = 1, definition.end(), definition.end()
            for token in re.finditer(STRING + r"|[{}]", ui_source[begin:]):
                if token.group() == "{":
                    depth += 1
                elif token.group() == "}":
                    depth -= 1
                    if not depth:
                        end = begin + token.start()
                        break
            literals += collect(ui_source[begin:end], seen)
        return literals

    def add(size, expression):
        used[size].update(c for c in collect(expression) if "\u4e00" <= c <= "\u9fff")

    for helper, size, index in (("text", 16, 2), ("title", 20, 2), ("header", 20, 0)):
        for args in calls(ui_source, helper):
            if len(args) > index:
                add(size, args[index])
    for helper, text_index, size_index in (("center", 1, 2), ("right", 2, 3), ("value_line", 3, 4)):
        for args in calls(ui_source, helper):
            if len(args) > size_index and args[size_index] in ("16U", "20U"):
                size = int(args[size_index][:-1])
                add(size, args[text_index])
                if helper == "value_line" and size == 20:
                    add(16, args[text_index])  # The value may downshift to 16px.
    return {size: sorted(chars) for size, chars in used.items()}, chinese


@lru_cache(maxsize=12)
def load_font(path, size):
    return ImageFont.truetype(str(path), size * SCALE)


def profile(path, size, chars, cell=None):
    font = load_font(path, size)
    boxes = [font.getbbox(c, anchor="ls") for c in chars]
    top, bottom = min(b[1] for b in boxes), max(b[3] for b in boxes)
    height = math.ceil((bottom - top) / SCALE) + 2 * PAD
    if cell:
        height = cell[1]
    baseline = math.floor((height * SCALE - (bottom - top)) / 2 - top)
    return font, height, baseline, cell


def raster(metrics, char):
    font, height, baseline, cell = metrics
    left, _, right, _ = font.getbbox(char, anchor="ls")
    origin = HPAD * SCALE - min(0, left)
    width = math.ceil((max(font.getlength(char), right) - min(0, left)) / SCALE) + 2 * HPAD
    if char == " ":
        width = math.ceil(font.getlength(char) / SCALE)
    if cell:
        width = cell[0]
        origin = round((width * SCALE - font.getlength(char)) / 2)
    # Render on a larger canvas first, then check that no ink lies outside the cell.
    full = Image.new("L", ((width + 2 * GUARD) * SCALE, (height + 2 * GUARD) * SCALE))
    ImageDraw.Draw(full).text((origin + GUARD * SCALE, baseline + GUARD * SCALE),
                              char, font=font, fill=255, anchor="ls")
    full = full.resize((width + 2 * GUARD, height + 2 * GUARD), Image.Resampling.LANCZOS)
    quantized = full.point(lambda value: (value * 3 + 127) // 255)
    ink = quantized.getbbox()
    if ink is None:
        return (0, 0, width, 0, 0), []
    if not (ink[0] > GUARD and ink[1] > GUARD and
            ink[2] < GUARD + width and ink[3] < GUARD + height):
        raise ValueError(f"Glyph would be clipped: {char!r}, cell={width}x{height}, ink={ink}")
    bounds = (ink[0] - 1, ink[1] - 1, ink[2] + 1, ink[3] + 1)
    image = quantized.crop(bounds)
    values = list(image.tobytes())
    values += [0] * (-len(values) % 4)
    packed = [sum(values[i + j] << (6 - j * 2) for j in range(4))
              for i in range(0, len(values), 4)]
    return (image.width, image.height, width, bounds[0] - GUARD, bounds[1] - GUARD), packed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "ThirdParty/HarmonyOS_Sans/harmonyos_sans.h")
    args = parser.parse_args()
    faces = {name: args.font_dir / f"HarmonyOS_Sans_SC_{name}.ttf"
             for name in ("Regular", "Medium", "Light")}
    for path in faces.values():
        if not path.is_file():
            parser.error(f"Missing official font: {path}")
    ui_source = (ROOT / "App/app_ui.c").read_text(encoding="utf-8")
    boot_source = (ROOT / "Core/Src/main.c").read_text(encoding="utf-8")
    subsets, chinese = font_characters(ui_source, boot_source)
    chars = subsets[14]
    large_chars = sorted(set("跌倒报警测试"))
    clock_chars = "0123456789:-"
    profiles = {size: profile(faces[face], size, chars)
                for size, face in ((14, "Regular"), (16, "Regular"), (20, "Medium"))}
    profiles[40] = profile(faces["Medium"], 40, large_chars)
    clock_profile = profile(faces["Light"], 52, clock_chars, (32, 52))
    pool, glyphs, large, clock = [], {}, [], []
    bitmap_offsets = {}

    def add(metrics, char):
        (width, height, advance, x, y), data = raster(metrics, char)
        key = bytes(data)
        offset = bitmap_offsets.get(key)
        if offset is None:
            offset = len(pool)
            bitmap_offsets[key] = offset
            pool.extend(data)
        return f"{{{offset}U,{width}U,{height}U,{advance}U,{x}U,{y}U}}"

    for size, subset in subsets.items():
        glyphs[size] = [f"    {{0x{ord(char):04X}U,{add(profiles[size], char)}}}," for char in subset]
    for char in large_chars:
        large.append(f"    {{0x{ord(char):04X}U,{add(profiles[40], char)}}},")
    for char in clock_chars:
        clock.append(f"    {add(clock_profile, char)},")
    if len(pool) > 65535:
        raise ValueError("Font bitmap pool exceeds uint16_t offsets")
    lines = ["/* Rendered with unmodified HarmonyOS Sans SC. Copyright 2021 Huawei Device Co., Ltd.",
             " * See LICENSE.txt. Metrics include complete ink bounds and blank guards."]
    for path in faces.values():
        lines.append(f" * {path.name} SHA-256: {hashlib.sha256(path.read_bytes()).hexdigest()}")
    lines += [" */", "#ifndef HARMONYOS_SANS_H", "#define HARMONYOS_SANS_H", "#include <stdint.h>",
              "typedef struct { uint16_t offset; uint8_t width,height,advance,x,y; } ui_font_bitmap_t;",
              "typedef struct { uint16_t code; ui_font_bitmap_t bitmap; } app_ui_glyph_t;",
              "typedef struct { uint32_t code; ui_font_bitmap_t bitmap; } app_ui_large_glyph_t;"]
    for size in (14, 16, 20, 40):
        lines.append(f"#define UI_FONT{size}_HEIGHT {profiles[size][1]}U")
    lines += ['#define UI_CLOCK_CHARS "0123456789:-"', "#define UI_CLOCK_WIDTH 32U",
              "#define UI_CLOCK_HEIGHT 52U", "static const uint8_t ui_font_pixels[] = {"]
    lines += ["    " + ",".join(f"0x{v:02X}" for v in pool[i:i + 20]) + ","
              for i in range(0, len(pool), 20)]
    lines.append("};")
    for size, rows in glyphs.items():
        lines += [f"static const app_ui_glyph_t ui_glyphs{size}[] = {{", *rows, "};"]
    lines += ["static const app_ui_large_glyph_t ui_large_glyphs[] = {", *large, "};",
              "static const ui_font_bitmap_t ui_clock_glyphs[] = {", *clock, "};", "#endif", ""]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines), encoding="utf-8")
    print(f"Generated {len(chinese)} Chinese glyphs, 95 ASCII glyphs; {len(pool)} bitmap bytes.")
    print("Line heights:", {size: profiles[size][1] for size in (14,16,20,40)})
    print("Glyphs by size:", {size: len(subset) for size, subset in subsets.items()})


if __name__ == "__main__":
    main()
