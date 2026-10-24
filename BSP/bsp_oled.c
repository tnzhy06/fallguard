
/***************************************************************************************
 * 本程序由江协科技创建并免费开源共享
 * 你可以任意查看、使用和修改，并应用到自己的项目之中
 * 程序版权归江协科技所有，任何人或组织不得将其据为己有
 *
 * 程序名称：				0.96寸OLED显示屏驱动程序（4针脚I2C接口）
 * 程序创建时间：			2023.10.24
 * 当前程序版本：			V1.1
 * 当前版本发布时间：		2023.12.8
 *
 * 江协科技官方网站：		jiangxiekeji.com
 * 江协科技官方淘宝店：	jiangxiekeji.taobao.com
 * 程序介绍及更新动态：	jiangxiekeji.com/tutorial/oled.html
 *
 * 如果你发现程序中的漏洞或者笔误，可通过邮件向我们反馈：feedback@jiangxiekeji.com
 * 发送邮件之前，你可以先到更新动态页面查看最新程序，如果此问题已经修改，则无需再发邮件
 ***************************************************************************************
 */

/*
 * 本程序由zeruns二次修改
 * 修改内容：	从标准库版改成HAL库版，增加支持硬件I2C，可通过修改宏定义来选择是否启用硬件I2C
 * 修改日期：	2024.3.16
 * 博客：		https://blog.zeruns.tech
 * B站主页：	https://space.bilibili.com/8320520
*/

/* Adapted for fallguard C03 from zeruns commit
 * 4127e9defa9cfde4b82c87c8e3a4165dbfaa776c.
 * Selected framebuffer/font/drawing routines retain the upstream implementation.
 * PB4/PB5 transport, TIM2 timing and cooperative refresh are adapted here.
 * License: ThirdParty/zeruns_oled/LICENSE (LGPL-3.0).
 */
#include "bsp_oled.h"
#include "bsp_delay.h"
#include "main.h"
#include "oled_data.h"
#include <string.h>

#define OLED_ADDRESS (0x3CU << 1)
#define OLED_BLOCK_SIZE 16U

static uint8_t OLED_DisplayBuf[8][128];
static bsp_oled_state_t oled_state;
static uint32_t oled_power_tick;
static uint8_t oled_init_index;
static uint8_t oled_page;
static uint8_t oled_column;
static uint8_t oled_busy;
static uint8_t oled_display_on;

/* The panel stays off until the first complete frame has replaced power-on RAM. */
static const uint8_t oled_commands[] = {
    0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
    0xA1, 0xC8, 0xDA, 0x12, 0x81, 0xCF, 0xD9, 0xF1,
    0xDB, 0x30, 0xA4, 0xA6, 0x8D, 0x14
};

static void oled_scl(GPIO_PinState level)
{
    HAL_GPIO_WritePin(OLED_SCL_GPIO_Port, OLED_SCL_Pin, level);
}

static void oled_sda(GPIO_PinState level)
{
    HAL_GPIO_WritePin(OLED_SDA_GPIO_Port, OLED_SDA_Pin, level);
}

static void oled_start(void)
{
    oled_sda(GPIO_PIN_SET);
    oled_scl(GPIO_PIN_SET);
    BSP_DELAY_Us(5U);
    oled_sda(GPIO_PIN_RESET);
    BSP_DELAY_Us(5U);
    oled_scl(GPIO_PIN_RESET);
}

static void oled_stop(void)
{
    oled_sda(GPIO_PIN_RESET);
    BSP_DELAY_Us(5U);
    oled_scl(GPIO_PIN_SET);
    BSP_DELAY_Us(5U);
    oled_sda(GPIO_PIN_SET);
    BSP_DELAY_Us(5U);
}

static uint8_t oled_send_byte(uint8_t value)
{
    uint8_t ack;

    for (uint8_t i = 0; i < 8U; ++i)
    {
        oled_sda((value & 0x80U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        BSP_DELAY_Us(5U);
        oled_scl(GPIO_PIN_SET);
        BSP_DELAY_Us(5U);
        oled_scl(GPIO_PIN_RESET);
        value <<= 1;
    }

    /* Release open-drain SDA before the slave drives its ACK bit. */
    oled_sda(GPIO_PIN_SET);
    BSP_DELAY_Us(5U);
    oled_scl(GPIO_PIN_SET);
    BSP_DELAY_Us(5U);
    ack = (HAL_GPIO_ReadPin(OLED_SDA_GPIO_Port, OLED_SDA_Pin) == GPIO_PIN_RESET);
    oled_scl(GPIO_PIN_RESET);
    return ack;
}

static uint8_t oled_write(uint8_t control, const uint8_t *data, uint8_t count)
{
    uint8_t ok;

    oled_start();
    ok = oled_send_byte(OLED_ADDRESS);
    if (ok != 0U)
    {
        ok = oled_send_byte(control);
    }
    for (uint8_t i = 0; (i < count) && (ok != 0U); ++i)
    {
        ok = oled_send_byte(data[i]);
    }
    oled_stop();
    return ok;
}

void BSP_OLED_Init(void)
{
    oled_scl(GPIO_PIN_SET);
    oled_sda(GPIO_PIN_SET);
    BSP_OLED_Clear();
    oled_power_tick = HAL_GetTick();
    oled_state = BSP_OLED_WAIT_POWER;
    oled_init_index = 0U;
    oled_page = 0U;
    oled_column = 0U;
    oled_busy = 0U;
    oled_display_on = 0U;
}

bsp_oled_state_t BSP_OLED_GetState(void)
{
    return oled_state;
}

uint8_t BSP_OLED_IsBusy(void)
{
    return oled_busy;
}

void BSP_OLED_Update(void)
{
    if ((oled_state == BSP_OLED_READY) && (oled_busy == 0U))
    {
        oled_page = 0U;
        oled_column = 0U;
        oled_busy = 1U;
    }
}

void BSP_OLED_Poll(void)
{
    if (oled_state == BSP_OLED_WAIT_POWER)
    {
        if ((uint32_t)(HAL_GetTick() - oled_power_tick) < 100U)
        {
            return;
        }
        if ((HAL_GPIO_ReadPin(OLED_SCL_GPIO_Port, OLED_SCL_Pin) == GPIO_PIN_RESET) ||
            (HAL_GPIO_ReadPin(OLED_SDA_GPIO_Port, OLED_SDA_Pin) == GPIO_PIN_RESET))
        {
            oled_state = BSP_OLED_ERROR;
            return;
        }
        oled_state = BSP_OLED_CONFIGURING;
    }

    if (oled_state == BSP_OLED_CONFIGURING)
    {
        if (oled_write(0x00U, &oled_commands[oled_init_index], 1U) == 0U)
        {
            oled_state = BSP_OLED_ERROR;
            return;
        }
        ++oled_init_index;
        if (oled_init_index == sizeof(oled_commands))
        {
            oled_state = BSP_OLED_READY;
        }
        return;
    }

    if ((oled_state == BSP_OLED_READY) && (oled_busy != 0U))
    {
        uint8_t cursor[3];

        cursor[0] = (uint8_t)(0xB0U | oled_page);
        cursor[1] = (uint8_t)(0x10U | (oled_column >> 4));
        cursor[2] = (uint8_t)(oled_column & 0x0FU);

        if ((oled_write(0x00U, cursor, sizeof(cursor)) == 0U) ||
            (oled_write(0x40U, &OLED_DisplayBuf[oled_page][oled_column],
                        OLED_BLOCK_SIZE) == 0U))
        {
            oled_state = BSP_OLED_ERROR;
            oled_busy = 0U;
            return;
        }

        oled_column += OLED_BLOCK_SIZE;
        if (oled_column == 128U)
        {
            oled_column = 0U;
            ++oled_page;
            if (oled_page == 8U)
            {
                oled_busy = 0U;
                if (oled_display_on == 0U)
                {
                    const uint8_t display_on = 0xAFU;
                    if (oled_write(0x00U, &display_on, 1U) == 0U)
                    {
                        oled_state = BSP_OLED_ERROR;
                        return;
                    }
                    oled_display_on = 1U;
                }
            }
        }
    }
}

void BSP_OLED_Clear(void)
{
    uint8_t i, j;
    for (j = 0; j < 8; j++)
    {
        for (i = 0; i < 128; i++)
        {
            OLED_DisplayBuf[j][i] = 0x00;
        }
    }
}

void BSP_OLED_ClearArea(uint8_t X, uint8_t Y, uint8_t Width, uint8_t Height)
{
    uint8_t i, j;

    if (X > 127) { return; }
    if (Y > 63) { return; }
    if (X + Width > 128) { Width = 128 - X; }
    if (Y + Height > 64) { Height = 64 - Y; }

    for (j = Y; j < Y + Height; j++)
    {
        for (i = X; i < X + Width; i++)
        {
            OLED_DisplayBuf[j / 8][i] &= ~(0x01 << (j % 8));
        }
    }
}

void BSP_OLED_Reverse(void)
{
    uint8_t i, j;
    for (j = 0; j < 8; j++)
    {
        for (i = 0; i < 128; i++)
        {
            OLED_DisplayBuf[j][i] ^= 0xFF;
        }
    }
}

void BSP_OLED_ShowChar(uint8_t X, uint8_t Y, char Char, uint8_t FontSize)
{
    if ((Char < ' ') || (Char > '~')) { Char = '?'; }
    if (FontSize == BSP_OLED_8X16)
    {

        BSP_OLED_ShowImage(X, Y, 8, 16, OLED_F8x16[Char - ' ']);
    } else if (FontSize == BSP_OLED_6X8)
    {

        BSP_OLED_ShowImage(X, Y, 6, 8, OLED_F6x8[Char - ' ']);
    }
}

void BSP_OLED_ShowImage(uint8_t X, uint8_t Y, uint8_t Width, uint8_t Height, const uint8_t *Image)
{
    uint8_t i, j;

    if ((Width == 0U) || (Height == 0U)) { return; }
    if (X > 127) { return; }
    if (Y > 63) { return; }

    BSP_OLED_ClearArea(X, Y, Width, Height);

    for (j = 0; j < (Height - 1) / 8 + 1; j++) {

        for (i = 0; i < Width; i++) {

            if (X + i > 127) { break; }
            if (Y / 8 + j > 7) { return; }

            uint8_t pixels = Image[j * Width + i];
            if (((j + 1U) * 8U > Height) && ((Height % 8U) != 0U))
            {
                pixels &= (uint8_t)((1U << (Height % 8U)) - 1U);
            }
            OLED_DisplayBuf[Y / 8 + j][X + i] |= pixels << (Y % 8);

            if (Y / 8 + j + 1 > 7) { continue; }

            OLED_DisplayBuf[Y / 8 + j + 1][X + i] |= pixels >> (8 - Y % 8);
        }
    }
}

void BSP_OLED_DrawPoint(int16_t X, int16_t Y)
{
    /* Circle coordinates can be negative; reject them before any narrowing. */
    if ((X < 0) || (X > 127) || (Y < 0) || (Y > 63)) { return; }

    OLED_DisplayBuf[Y / 8][X] |= 0x01 << (Y % 8);
}

void BSP_OLED_DrawLine(uint8_t X0, uint8_t Y0, uint8_t X1, uint8_t Y1)
{
    int16_t x, y, dx, dy, d, incrE, incrNE, temp;
    int16_t x0 = X0, y0 = Y0, x1 = X1, y1 = Y1;
    uint8_t yflag = 0, xyflag = 0;

    if (y0 == y1)
    {

        if (x0 > x1) {
            temp = x0;
            x0   = x1;
            x1   = temp;
        }

        for (x = x0; x <= x1; x++) {
            BSP_OLED_DrawPoint(x, y0);
        }
    } else if (x0 == x1)
    {

        if (y0 > y1) {
            temp = y0;
            y0   = y1;
            y1   = temp;
        }

        for (y = y0; y <= y1; y++) {
            BSP_OLED_DrawPoint(x0, y);
        }
    } else
    {

        if (x0 > x1)
        {

            temp = x0;
            x0   = x1;
            x1   = temp;
            temp = y0;
            y0   = y1;
            y1   = temp;
        }

        if (y0 > y1)
        {

            y0 = -y0;
            y1 = -y1;

            yflag = 1;
        }

        if (y1 - y0 > x1 - x0)
        {

            temp = x0;
            x0   = y0;
            y0   = temp;
            temp = x1;
            x1   = y1;
            y1   = temp;

            xyflag = 1;
        }

        dx     = x1 - x0;
        dy     = y1 - y0;
        incrE  = 2 * dy;
        incrNE = 2 * (dy - dx);
        d      = 2 * dy - dx;
        x      = x0;
        y      = y0;

        if (yflag && xyflag) {
            BSP_OLED_DrawPoint(y, -x);
        } else if (yflag) {
            BSP_OLED_DrawPoint(x, -y);
        } else if (xyflag) {
            BSP_OLED_DrawPoint(y, x);
        } else {
            BSP_OLED_DrawPoint(x, y);
        }

        while (x < x1)
        {
            x++;
            if (d < 0)
            {
                d += incrE;
            } else
            {
                y++;
                d += incrNE;
            }

            if (yflag && xyflag) {
                BSP_OLED_DrawPoint(y, -x);
            } else if (yflag) {
                BSP_OLED_DrawPoint(x, -y);
            } else if (xyflag) {
                BSP_OLED_DrawPoint(y, x);
            } else {
                BSP_OLED_DrawPoint(x, y);
            }
        }
    }
}

void BSP_OLED_DrawRectangle(uint8_t X, uint8_t Y, uint8_t Width, uint8_t Height, uint8_t IsFilled)
{
    uint8_t i, j;
    if ((X > 127U) || (Y > 63U) || (Width == 0U) || (Height == 0U)) { return; }
    if (X + Width > 128U) { Width = 128U - X; }
    if (Y + Height > 64U) { Height = 64U - Y; }
    if (!IsFilled)
    {

        for (i = X; i < X + Width; i++) {
            BSP_OLED_DrawPoint(i, Y);
            BSP_OLED_DrawPoint(i, Y + Height - 1);
        }

        for (i = Y; i < Y + Height; i++) {
            BSP_OLED_DrawPoint(X, i);
            BSP_OLED_DrawPoint(X + Width - 1, i);
        }
    } else
    {

        for (i = X; i < X + Width; i++) {

            for (j = Y; j < Y + Height; j++) {

                BSP_OLED_DrawPoint(i, j);
            }
        }
    }
}

void BSP_OLED_DrawCircle(uint8_t X, uint8_t Y, uint8_t Radius, uint8_t IsFilled)
{
    int16_t x, y, d, j;

    d = 1 - Radius;
    x = 0;
    y = Radius;

    BSP_OLED_DrawPoint(X + x, Y + y);
    BSP_OLED_DrawPoint(X - x, Y - y);
    BSP_OLED_DrawPoint(X + y, Y + x);
    BSP_OLED_DrawPoint(X - y, Y - x);

    if (IsFilled)
    {

        for (j = -y; j < y; j++) {

            BSP_OLED_DrawPoint(X, Y + j);
        }
    }

    while (x < y)
    {
        x++;
        if (d < 0)
        {
            d += 2 * x + 1;
        } else
        {
            y--;
            d += 2 * (x - y) + 1;
        }

        BSP_OLED_DrawPoint(X + x, Y + y);
        BSP_OLED_DrawPoint(X + y, Y + x);
        BSP_OLED_DrawPoint(X - x, Y - y);
        BSP_OLED_DrawPoint(X - y, Y - x);
        BSP_OLED_DrawPoint(X + x, Y - y);
        BSP_OLED_DrawPoint(X + y, Y - x);
        BSP_OLED_DrawPoint(X - x, Y + y);
        BSP_OLED_DrawPoint(X - y, Y + x);

        if (IsFilled)
        {

            for (j = -y; j < y; j++) {

                BSP_OLED_DrawPoint(X + x, Y + j);
                BSP_OLED_DrawPoint(X - x, Y + j);
            }

            for (j = -x; j < x; j++) {

                BSP_OLED_DrawPoint(X - y, Y + j);
                BSP_OLED_DrawPoint(X + y, Y + j);
            }
        }
    }
}

void BSP_OLED_ShowString(uint8_t X, uint8_t Y, const char *String, uint8_t FontSize)
{
    uint16_t cursor = X;

    if ((FontSize != BSP_OLED_6X8) && (FontSize != BSP_OLED_8X16))
    {
        return;
    }
    while ((*String != '\0') && (cursor < 128U))
    {
        BSP_OLED_ShowChar((uint8_t)cursor, Y, *String++, FontSize);
        cursor += FontSize;
    }
}

void BSP_OLED_ShowChinese(uint8_t X, uint8_t Y, const char *Chinese)
{
    uint16_t cursor = X;
    char single[OLED_CHN_CHAR_WIDTH + 1U];

    while ((*Chinese != '\0') && (cursor < 128U))
    {
        uint8_t index = 0U;

        if ((Chinese[1] == '\0') || (Chinese[2] == '\0'))
        {
            break;
        }
        memcpy(single, Chinese, OLED_CHN_CHAR_WIDTH);
        /* The original writes [width + 1], one byte beyond this four-byte array. */
        single[OLED_CHN_CHAR_WIDTH] = '\0';
        while (OLED_CF16x16[index].Index[0] != '\0')
        {
            if (strcmp(OLED_CF16x16[index].Index, single) == 0)
            {
                break;
            }
            ++index;
        }
        BSP_OLED_ShowImage((uint8_t)cursor, Y, 16U, 16U, OLED_CF16x16[index].Data);
        Chinese += OLED_CHN_CHAR_WIDTH;
        cursor += 16U;
    }
}
