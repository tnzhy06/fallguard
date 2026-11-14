"""Run the real LCD/UI C code on a host compiler and export screen previews.

Requires a C99 compiler (GCC/Clang/TinyCC) and Pillow, not STM32 hardware.
python tools/preview_ui.py --cc path/to/tcc.exe
"""
import argparse
import subprocess
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--output", type=Path, default=ROOT / "build/ui-preview")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if not (ROOT / "App/app_config.h").exists():
        (output / "app_config.h").write_bytes((ROOT / "App/app_config.example.h").read_bytes())
    executable = output / "test_lcd.exe"
    includes = [ROOT / p for p in ("tests/lcd_host", "App", "BSP", "ThirdParty/FloydFish_ST7789", "ThirdParty/NotoSansSC")]
    includes.append(output)
    sources = [ROOT / p for p in ("tests/lcd_host/test_lcd.c", "App/app_ui.c", "BSP/bsp_lcd.c",
                                  "ThirdParty/FloydFish_ST7789/st7789.c")]
    command = [args.cc, "-std=c99", "-Wall", *[f"-I{p}" for p in includes],
               *map(str, sources), "-o", str(executable)]
    subprocess.run(command, check=True, cwd=ROOT)
    subprocess.run([str(executable)], check=True, cwd=output)
    assert (output / "alarm.ppm").read_bytes() == (output / "alarm_k1.ppm").read_bytes(), "K1 must preserve the alarm screen"
    assert (output / "home.ppm").read_bytes() == (output / "cancel_home.ppm").read_bytes(), "Cancel must restore the home screen"
    names = ["home", "position", "satellite", "motion", "network", "alarm",
             "calibration", "last_position", "weak_signal", "sensor_error", "offline", "test_alarm"]
    sheet = Image.new("RGB", (3 * 264 + 24, 4 * 282 + 24), "#182332")
    draw = ImageDraw.Draw(sheet)
    for i, name in enumerate(names):
        screen = Image.open(output / f"{name}.ppm")
        screen.save(output / f"{name}.png")
        x, y = 24 + (i % 3) * 264, 24 + (i // 3) * 282
        sheet.paste(screen, (x, y))
        draw.text((x, y + 246), name, fill="#cbd8e6")
    sheet.save(output / "overview.png")
    # Show the actual header pixels enlarged for inspecting icon spacing/strokes.
    header = Image.open(output / "home.ppm").crop((0, 0, 240, 38))
    header.resize((720, 114), Image.Resampling.NEAREST).save(output / "header.png")
    print(f"Preview: {output / 'overview.png'}")


if __name__ == "__main__":
    main()
