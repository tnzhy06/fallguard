# HarmonyOS Sans 界面字体

整个界面统一使用 HarmonyOS Sans SC，包括中文、英文、数字、标点、启动日志和大时钟。

- 官方资源页：https://developer.huawei.com/consumer/cn/design/resource-V1/
- 官方字体包：https://developer.huawei.com/images/download/general/HarmonyOS-Sans.zip
- 版权所有：Copyright 2021 Huawei Device Co., Ltd.
- 许可正文：[LICENSE.txt](LICENSE.txt)，随官方字体包提供。软件启动页显示 HarmonyOS Sans 署名。

使用官方包 `HarmonyOS Sans/HarmonyOS_Sans_SC/` 中的未修改原始 TTF：正文 14/16px 使用 Regular，标题 20/40px 使用 Medium，大时钟 52px 使用 Light。中文、英文和数字均采用这一组字体，不进行字体文件裁剪、轮廓修改或伪粗体处理。

`harmonyos_sans.h` 是本应用绘制所需的 2bpp 点阵图像资源。生成文件记录三个原始 TTF 的 SHA-256，固件无需加载完整字体，也无需在 MCU 上运行字体引擎。原始下载包和 TTF 不随工程保存；需要重新生成时，从上述官方地址下载并解压字体包。

重新生成：

```sh
python tools/generate_ui_font.py --font-dir "path/to/HarmonyOS_Sans_SC"
```

生成器从 `App/app_ui.c` 与 `Core/Src/main.c` 的字符串中收集汉字，绘制全部使用的中文字符、95 个可打印 ASCII 字符及大时钟字符；以四倍分辨率绘制并量化到四级灰度。字宽与绘制使用同一字重，所有字符共用对应字号的基线。

字模按字号分别建立索引：14px 保留全部界面及启动日志汉字，16/20px 根据 `text/title/header/center/right/value_line` 调用中的文字及状态函数收集实际需要的汉字，各字号保留全部可打印 ASCII，40px 保留报警标题。完全相同的位图共享存储。字号基线仍按完整字符集合计算，因此裁掉未使用的字号资源不会移动或改变已有字形。当前位图池约 30KB，字模与索引合计约 36KB。

生成器能跟踪文字表达式直接调用的状态函数。若新增通过变量或格式化生成的中文大字号文字，应扩展 `font_characters()` 的收集规则；不能依靠 ASCII 数值规则推断中文。新增文案后应重新生成字模，并在屏幕上检查对应页面和状态是否存在缺字。

字号并不等于完整字形的高度。生成器按每组字符的实际上下边界计算行高，并在较大的画布上检查所有笔画均落在字框内。14/16/20/40px 当前分别使用 20/22/27/42px 行高；大时钟使用 32×52px 字框。每个字形独立保存水平步进、偏移和带空白保护边的位图，避免上下伸出的笔画、英文降部或相邻字形被裁掉。布局坐标的 `y` 表示行框顶部，应通过 `BSP_LCD_TextHeight()` 获取高度。

字模生成器会检查字形是否越过字框边界。主机测试和专用预览脚本已移除；新增界面内容后，在开发板上检查字形、页面安全边距、文字与图标间距，以及长数值的轮播显示。
