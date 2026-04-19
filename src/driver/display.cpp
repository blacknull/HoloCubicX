#include "display.h"
#include "network.h"
#include "lv_port_indev.h"
#include "lv_demo_encoder.h"
#include "gui_lock.h"
#include "common.h"

#define LV_HOR_RES_MAX_LEN 80 // 24

// LVGL 绘制的 240x240 在面板上的偏移（面板 240x280，可视区为 y=20..259）
#define DISP_Y_OFFSET 20

static lv_disp_draw_buf_t disp_buf;
static lv_disp_drv_t disp_drv;
static lv_color_t buf[SCREEN_HOR_RES * LV_HOR_RES_MAX_LEN];

void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    tft->setAddrWindow(area->x1, area->y1, w, h);
    tft->startWrite();
    // tft->writePixels(&color_p->full, w * h);
    tft->pushColors(&color_p->full, w * h, true);
    tft->endWrite();
    // Initiate DMA - blocking only if last DMA is not complete
    // tft->pushImageDMA(area->x1, area->y1, w, h, bitmap, &color_p->full);

    lv_disp_flush_ready(disp);
}

void Display::init(uint8_t rotation, uint8_t backLight)
{
    lv_init();

    tft->begin(); /* TFT init */
    tft->fillScreen(TFT_BLACK);
    tft->writecommand(ST7789_DISPON); // Display on
    // tft->fillScreen(BLACK);

    setBackLight(0); // 设置亮度最低，防止显示开机花屏

    // 尝试读取屏幕数据作为屏幕检测的依旧
    // uint8_t ret = tft->readcommand8(0x01, TFT_MADCTL);
    // log_if("TFT read -> %u\r\n", ret);

    // rotation 低2位：标准旋转（0~3）；bit2 (值 4)：左右镜像（分光棱镜/反射显示使用）
    tft->setRotation(rotation & 0x03);
    if (rotation & 0x04)
    {
        // 在标准 MADCTL 基础上叠加 MX 位翻转水平方向
        uint8_t madctl = 0;
        switch (rotation & 0x03)
        {
        case 0: madctl = TFT_MAD_MX | TFT_MAD_COLOR_ORDER; break;
        case 1: madctl = TFT_MAD_MV | TFT_MAD_COLOR_ORDER; break;
        case 2: madctl = TFT_MAD_MY | TFT_MAD_COLOR_ORDER; break;
        case 3: madctl = TFT_MAD_MX | TFT_MAD_MY | TFT_MAD_MV | TFT_MAD_COLOR_ORDER; break;
        }
        tft->writecommand(TFT_MADCTL);
        tft->writedata(madctl);
    }

    setBackLight(backLight / 100.0); // 设置亮度

    lv_disp_draw_buf_init(&disp_buf, buf, NULL, SCREEN_HOR_RES * LV_HOR_RES_MAX_LEN);

    /*Initialize the display*/
    lv_disp_drv_init(&disp_drv);

    /* 1. 设置逻辑分辨率 */
    disp_drv.hor_res = SCREEN_HOR_RES;
    disp_drv.ver_res = SCREEN_VER_RES - 2 * DISP_Y_OFFSET; // 物理分辨率减去上下偏移

    /* 2. 设置完整的物理分辨率 */
    disp_drv.physical_hor_res = SCREEN_HOR_RES;
    disp_drv.physical_ver_res = SCREEN_VER_RES;

    /* 3. 设置偏移量 */
    disp_drv.offset_x = 0;
    disp_drv.offset_y = DISP_Y_OFFSET;

    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &disp_buf;
    disp_drv.user_data = tft;

    // 开启 LV_COLOR_SCREEN_TRANSP 屏幕具有透明和不透明样式
    lv_disp_drv_register(&disp_drv);
}

void Display::routine()
{
    LVGL_OPERATE_LOCK(lv_timer_handler();)
}

void Display::setBackLight(float duty)
{
    duty = constrain(duty, 0, 1);
#if defined(TFT_BACKLIGHT_ON) && (TFT_BACKLIGHT_ON == LOW)
    // 低电平点亮：亮度越高，PWM 占空比越低
    duty = 1 - duty;
#endif
    analogWrite(TFT_BL, (int)(duty * 255));
}
