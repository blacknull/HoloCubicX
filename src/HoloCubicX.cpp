/***************************************************
  HoloCubic多功能固件源码
  （项目中若参考本工程源码，请注明参考来源）

  聚合多种APP，内置天气、时钟、相册、特效动画、视频播放、视频投影、
  浏览器文件修改。（各APP具体使用参考说明书）

  Github repositories：https://github.com/ClimbSnail/HoloCubic_AIO

  Last review/edit by ClimbSnail: 2023/01/14
 ****************************************************/
#include "driver/lv_port_indev.h"
#include "driver/lv_port_fs.h"

#include "common.h"
#include "gui_lock.h"
#include "sys/app_controller.h"

#include "app/app_conf.h"

#include <SPIFFS.h>
#include <esp32-hal.h>
#include <esp32-hal-timer.h>
#include <esp_task_wdt.h>


bool isCheckAction = false;

/*** Component objects **7*/
ImuAction *act_info;           // 存放mpu6050返回的数据
AppController *app_controller; // APP控制器

TaskHandle_t handleTaskRefresh;

void refreshScreen(void *parameter)
{
    while (1)
    {
        LVGL_OPERATE_LOCK(lv_task_handler();) // 阻塞
        vTaskDelay(1000.0 / 60 / portTICK_PERIOD_MS);
    }
}


TimerHandle_t xTimerAction = NULL;
void actionCheckHandle(TimerHandle_t xTimer)
{
    // 标志需要检测动作
    isCheckAction = true;
}

void my_print(const char *buf)
{
    log_i("%s", buf);
    Serial.flush();
}

//#define MY_LVGL_DEBUG
#ifdef MY_LVGL_DEBUG
static lv_disp_draw_buf_t draw_buf;
const int BUF_SIZE = SCREEN_HOR_RES * SCREEN_VER_RES / 4;
static lv_color_t my_buf[BUF_SIZE];
/* Display flushing */
void my_disp_flush2( lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p )
{
    uint32_t w = ( area->x2 - area->x1 + 1 );
    uint32_t h = ( area->y2 - area->y1 + 1 );

    tft->startWrite();
    tft->setAddrWindow( area->x1, area->y1, w, h );
    tft->pushColors( ( uint16_t * )&color_p->full, w * h, true );
    tft->endWrite();

    lv_disp_flush_ready( disp );
}

lv_obj_t *screen1, *screen2;
void lv_example_gif_1(void)
{
    lv_obj_t * img;
    LV_IMG_DECLARE(img_bulb_gif);
    //LV_IMAGE_DECLARE(earth);

    img = lv_gif_create(screen1);
    lv_gif_set_src(img, &img_bulb_gif);//earth);    
    //lv_obj_align(img, LV_ALIGN_LEFT_MID, 50, 50);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);    

    /*
    img = lv_gif_create(screen1);
    // Assuming a File system is attached to letter 'A'
    // E.g. set LV_USE_FS_STDIO 'A' in lv_conf.h 
    lv_gif_set_src(img, "L:/pig64.gif");//earth120.gif");
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 40);
    //lv_obj_align(img, LV_ALIGN_TOP_LEFT, 0, 0);
//*/
}


void my_lvgl_init() {
    lv_init();

    tft->begin();          /* TFT init */
    tft->setRotation( 4 ); /* Landscape orientation, flipped */

    lv_disp_draw_buf_init( &draw_buf, my_buf, NULL, BUF_SIZE);

    /*Initialize the display*/
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init( &disp_drv );
    /*Change the following line to your display resolution*/
    disp_drv.hor_res = SCREEN_HOR_RES;
    disp_drv.ver_res = SCREEN_VER_RES;
    disp_drv.flush_cb = my_disp_flush2;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register( &disp_drv );    

    // a test screen
    screen1 = lv_obj_create(NULL);
    screen2 = lv_obj_create(NULL);

    // 设置screen1 - 蓝色背景带标签
    lv_obj_set_style_bg_color(screen1, lv_color_hex(0x003a57), LV_PART_MAIN);
    lv_obj_t * label1 = lv_label_create(screen1);
    lv_label_set_text(label1, "SCREEN 1");
    lv_obj_set_style_text_color(label1, lv_color_white(), 0);
    lv_obj_set_style_text_font(label1, &lv_font_montserrat_30, 0);
    lv_obj_center(label1);

    // 设置screen2 - 橙色背景带标签
    lv_obj_set_style_bg_color(screen2, lv_color_hex(0xed7f10), LV_PART_MAIN);
    lv_obj_t * label2 = lv_label_create(screen2);
    lv_label_set_text(label2, "SCREEN 2");
    lv_obj_set_style_text_color(label2, lv_color_white(), 0);
    lv_obj_set_style_text_font(label2, &lv_font_montserrat_30, 0);
    lv_obj_center(label2);

    // 初始加载screen1
    lv_scr_load(screen1);

    // gif test
    //lv_example_gif_1();

}

#endif /*MY_LVGL_DEBUG*/


void setup()
{
   // 初始化串口
    Serial.begin(115200);
    delay(3000);
    log_i("\nAIO (All in one) version " AIO_VERSION "\n");    
    Serial.flush();
    // MAC ID可用作芯片唯一标识
    log_i("ChipID(EfuseMac): ");
    log_i("%lld",ESP.getEfuseMac());
    // flash运行模式
    // log_i(F("FlashChipMode: "));
    // log_i(ESP.getFlashChipMode());
    // log_i(F("FlashChipMode value: FM_QIO = 0, FM_QOUT = 1, FM_DIO = 2, FM_DOUT = 3, FM_FAST_READ = 4, FM_SLOW_READ = 5, FM_UNKNOWN = 255"));

    app_controller = new AppController(); // APP控制器

    // 需要放在Setup里初始化SPIFFS（闪存文件系统）
    if (!SPIFFS.begin(true))
    {
        log_i("SPIFFS Mount Failed");
        return;
    }

    // config_read(NULL, &g_cfg);   // 旧的配置文件读取方式
    app_controller->read_config(&app_controller->sys_cfg);
    app_controller->read_config(&app_controller->mpu_cfg);
    app_controller->read_config(&app_controller->rgb_cfg);


    printHeapStackInfo(xTaskGetCurrentTaskHandle());
    //*
    // Init network
    if (!g_network.start_conn_wifi(app_controller->sys_cfg.ssid_0.c_str(),
                                    app_controller->sys_cfg.password_0.c_str())) {
        log_i("Network start_conn_wifi failed");
        return;
    }
    else {
        log_i("Network start_conn_wifi success");
    }
    printHeapStackInfo(xTaskGetCurrentTaskHandle());
    //*/

    /*** Init screen ***/
    screen.init(app_controller->sys_cfg.rotation, app_controller->sys_cfg.back_light);
    // my_lvgl_init();

    /*** Init on-board RGB ***/
    //rgb.init();
    //rgb.setBrightness(0.05).setRGB(0, 64, 64);


    /*** Init ambient-light sensor ***/
    //ambLight.init(ONE_TIME_H_RESOLUTION_MODE);

    /*** Init micro SD-Card ***/
    //tf.init();
    //lv_fs_fatfs_init();

    //*
    // 自动刷新屏幕
    BaseType_t taskRefreshReturned =
            xTaskCreate(refreshScreen,
                        "refreshScreen", 8 * 1024,
                        nullptr, TASK_LVGL_PRIORITY, &handleTaskRefresh);
    if (taskRefreshReturned != pdPASS) log_e("taskRefreshReturned != pdPASS");
    else log_i("taskRefreshReturned == pdPASS");
    //*/                

#if LV_USE_LOG
    lv_log_register_print_cb(my_print);
#endif /*LV_USE_LOG*/

    app_controller->init();

// 将APP"安装"到controller里
#if APP_TOMATO_USE
    app_controller->app_install(&tomato_app);
#endif
#if APP_WEB_SERVER_USE
    app_controller->app_install(&server_app);
#endif
#if APP_GAME_SNAKE_USE
    app_controller->app_install(&game_snake_app);
#endif
#if APP_WEATHER_USE
    app_controller->app_install(&weather_app);
#endif
#if APP_PICTURE_USE
    app_controller->app_install(&picture_app);
#endif
#if APP_MEDIA_PLAYER_USE
    app_controller->app_install(&media_app);
#endif
#if APP_FILE_MANAGER_USE
    app_controller->app_install(&file_manager_app);
#endif
#if APP_IDEA_ANIM_USE
    app_controller->app_install(&idea_app);
#endif
#if APP_SETTING_USE
    app_controller->app_install(&settings_app);
#endif
#if APP_GAME_2048_USE
    app_controller->app_install(&game_2048_app);
#endif
#if APP_ANNIVERSARY_USE
    app_controller->app_install(&anniversary_app);
#endif
#if APP_ARCHER_USE
    app_controller->app_install(&archer_app);
#endif
#if APP_PC_RESOURCE_USE
    app_controller->app_install(&pc_resource_app);
#endif
#if APP_LHLXW_USE
    app_controller->app_install(&LHLXW_app);
#endif

    // 优先显示屏幕 加快视觉上的开机时间
    app_controller->main_process(&mpu.action_info);

    /*** Init IMU as input device ***/
    // lv_port_indev_init();

    // TODO: 当前硬件 I2C 在无设备应答时会卡死 Wire 驱动，暂不启用 MPU 初始化
    /*
    mpu.init(app_controller->sys_cfg.mpu_order,
             app_controller->sys_cfg.auto_calibration_mpu,
             &app_controller->mpu_cfg); // 初始化比较耗时
    //*/

    /*** 以此作为MPU6050初始化完成的标志 ***/
    // 初始化RGB灯 HSV色彩模式
    //rgb_task_run(&app_controller->rgb_cfg);

    // 先初始化一次动作数据 防空指针
    act_info = new ImuAction;
    memset(act_info, 0, sizeof(ImuAction));
    act_info->active = ACTIVE_TYPE::UNKNOWN;
    act_info->isValid = false;


    // 定义一个mpu6050的动作检测定时器（每200ms将isCheckAction置为true）
    xTimerAction = xTimerCreate("Action Check",
                                200 / portTICK_PERIOD_MS,
                                pdTRUE, (void *)0, actionCheckHandle);
    xTimerStart(xTimerAction, 0);

    // 自启动APP
    app_controller->app_auto_start();
}

// 在cores/esp32/main.cpp 中启动为FreeRTOS task，优先级为1（最低）。
// FreeRTOSConfig.h中宏configUSE_PREEMPTION为1，是抢占式调度器。同优先级时间片轮转，高优先级抢占。
// ESP32-s3 双核
void loop()
{
//    screen.routine(); // 手动刷新屏幕
    yield();

    if (isCheckAction)
    {
        isCheckAction = false;
        //act_info = mpu.getAction(); // 更新姿态
        act_info->active = ACTIVE_TYPE::UNKNOWN;
    }

    // 无 IMU 硬件时的自动演示：每 5 秒循环切到下一个 APP
    // 步骤：RETURN 退出当前 APP → TURN_LEFT 在 launcher 选下一个 → DOWN_MORE 进入
    {
        static int sAutoStep = -1;      // -1: 空闲等待；0/1/2: 正在下发第 N 步手势
        static unsigned long sAutoStepDeadline = 5000;
        unsigned long now = GET_SYS_MILLIS();
        if (sAutoStep < 0 && now >= sAutoStepDeadline)
        {
            sAutoStep = 0;
            sAutoStepDeadline = now;
        }
        if (sAutoStep >= 0 && now >= sAutoStepDeadline)
        {
            switch (sAutoStep)
            {
            case 0: // 退出当前 APP，返回 launcher
                act_info->active = ACTIVE_TYPE::RETURN;
                act_info->isValid = true;
                sAutoStepDeadline = now + 1500; // 等退出动画
                break;
            case 1: // launcher 里移到下一个图标
                act_info->active = ACTIVE_TYPE::TURN_LEFT;
                act_info->isValid = true;
                sAutoStepDeadline = now + 500;
                break;
            case 2: // 进入选中的 APP
                act_info->active = ACTIVE_TYPE::DOWN_MORE;
                act_info->isValid = true;
                sAutoStepDeadline = now + 5000; // 在新 APP 停留 5 秒
                sAutoStep = -2;                 // 用 -2 标记下轮从头开始
                break;
            }
            if (sAutoStep == -2) sAutoStep = -1;
            else sAutoStep++;
        }
    }

    app_controller->main_process(act_info); // 运行当前进程
    // log_i(ambLight.getLux() / 50.0);
    // rgb.setBrightness(ambLight.getLux() / 500.0);

    // 处理LVGL任务
    //lv_task_handler();
}