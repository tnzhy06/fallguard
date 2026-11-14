# Noto Sans SC 界面字体

来源：https://github.com/google/fonts/tree/main/ofl/notosanssc

字体：`NotoSansSC[wght].ttf`，字重 500，采用 SIL Open Font License 1.1，见 [OFL.txt](OFL.txt)。

本目录的 `noto_sans_sc.h` 保存界面实际使用的全部字模。中文、英文、数字和标点统一来自上述开源字体，不再混用 ST7789 上游的 ASCII 点阵字体。

- 16/20px：界面全部汉字和 95 个可打印 ASCII 字符；ASCII 按字体字宽排版。
- 40px：报警标题的中文子集。
- 32×52px：主页时钟专用字符格，包含数字、冒号和横线，在目标尺寸直接栅格化，避免放大旧字模的锯齿。

全部字模采用每像素 2 位灰度，绘制时混合 RGB565 前景和背景。固件只链接已经生成的子集，不包含完整 TTF，也不需要运行时解析字体。

新增中文文案后，下载原字体并运行：

```sh
python -m pip install Pillow
python tools/generate_ui_font.py --font "path/to/NotoSansSC[wght].ttf"
```

生成器从 `App/app_ui.c` 自动提取汉字，输出至本目录的 `noto_sans_sc.h`，并记录原字体文件的 SHA-256 以便追溯。正常构建无需 Python 或字体转换工具。
