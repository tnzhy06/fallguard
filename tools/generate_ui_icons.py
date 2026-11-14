"""Generate the header's 20px status icons from vector geometry (Pillow).

All icons use the same cell and optical center. Rounded strokes are rendered
at 8x resolution and reduced to 2bpp masks for the STM32 strip renderer.
"""
import math
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
SIZE, SCALE = 20, 8


def canvas():
    image = Image.new("L", (SIZE * SCALE, SIZE * SCALE))
    return image, ImageDraw.Draw(image)


def stroke(draw, points, width=1.6):
    points = [(round(x * SCALE), round(y * SCALE)) for x, y in points]
    draw.line(points, fill=255, width=round(width * SCALE), joint="curve")
    r = width * SCALE / 2
    for x, y in (points[0], points[-1]):
        draw.ellipse((x - r, y - r, x + r, y + r), fill=255)


def cubic(a, b, c, d):
    points = []
    for i in range(33):
        t, u = i / 32, 1 - i / 32
        points.append(tuple(u**3 * a[j] + 3 * u*u*t * b[j] +
                            3 * u*t*t * c[j] + t**3 * d[j] for j in (0, 1)))
    return points


def wifi():
    image, draw = canvas()
    for radius in (12, 8, 4):
        points = [(10 + radius * math.cos(math.radians(a)),
                   16 + radius * math.sin(math.radians(a))) for a in range(-135, -44)]
        stroke(draw, points)
    draw.ellipse((9 * SCALE, 15 * SCALE, 11 * SCALE, 17 * SCALE), fill=255)
    return image


def mqtt_link():
    image, draw = canvas()
    left = [(8, 5), (6, 5)] + cubic((6, 5), (3, 5), (1, 7), (1, 10))
    left += cubic((1, 10), (1, 13), (3, 15), (6, 15)) + [(8, 15)]
    right = [(12, 5), (14, 5)] + cubic((14, 5), (17, 5), (19, 7), (19, 10))
    right += cubic((19, 10), (19, 13), (17, 15), (14, 15)) + [(12, 15)]
    stroke(draw, left)
    stroke(draw, right)
    stroke(draw, [(6, 10), (14, 10)])
    return image


def signal_bar(index):
    image, draw = canvas()
    x, height = 1 + index * 5, 4 + index * 4
    draw.rounded_rectangle((x * SCALE, (18 - height) * SCALE,
                            (x + 2.4) * SCALE, 18 * SCALE), radius=SCALE, fill=255)
    return image


def packed(image):
    image = image.resize((SIZE, SIZE), Image.Resampling.LANCZOS)
    levels = [(v * 3 + 127) // 255 for v in image.tobytes()]
    return ", ".join(f"0x{sum(levels[i+j] << (6-2*j) for j in range(4)):02X}"
                     for i in range(0, len(levels), 4))


def main():
    lines = ["/* Generated from vector geometry by tools/generate_ui_icons.py.",
             " * 20x20 pixels, packed 2bpp, MSB first; zero pixels are transparent. */",
             "#ifndef APP_UI_ICONS_H", "#define APP_UI_ICONS_H", "#include <stdint.h>",
             f"#define UI_STATUS_ICON_SIZE {SIZE}U",
             f"static const uint8_t ui_wifi_icon[100] = {{{packed(wifi())}}};",
             f"static const uint8_t ui_mqtt_icon[100] = {{{packed(mqtt_link())}}};",
             "static const uint8_t ui_signal_icons[4][100] = {"]
    lines.extend(f"    {{{packed(signal_bar(i))}}}," for i in range(4))
    lines += ["};", "#endif", ""]
    output = ROOT / "App/app_ui_icons.h"
    output.write_text("\n".join(lines), encoding="utf-8")
    print(f"Generated status icons: {output}")


if __name__ == "__main__":
    main()
