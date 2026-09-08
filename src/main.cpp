#include <Arduino.h>
#include <SPI.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include "ui/ui.h"
#include "ui/images.h"
#include "uart_driver.h"
#include "ui_manager.h"
#include "data_simulator.h"


/* ================= TOUCH PINS ================= */
#define XPT2046_IRQ   36
#define XPT2046_MOSI  32
#define XPT2046_MISO  39
#define XPT2046_CLK   25
#define XPT2046_CS    33

/* ================= LCD ================= */
#define LCD_BACKLIGHT_PIN 21
static const uint16_t screenWidth  = 320;
static const uint16_t screenHeight = 240;
#define LED_PIN 27
/* ================= LVGL BUFFER ================= */
static lv_color_t buf[screenWidth * 20];

/* ================= OBJECTS ================= */
TFT_eSPI tft;
XPT2046_Touchscreen touch(XPT2046_CS, XPT2046_IRQ);
TelemetryData telemetry;



void my_disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *color_p)
{
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t *)color_p, w * h, true);
    tft.endWrite();

    lv_display_flush_ready(disp);
}

void my_touch_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    if (!touch.touched()) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    TS_Point p = touch.getPoint();

    // Raw calibration values (typical CYD)
    const int X_MIN = 300;
    const int X_MAX = 3800;
    const int Y_MIN = 300;
    const int Y_MAX = 3800;

    // Step 1: swap axes
    int16_t x = map(p.y, Y_MIN, Y_MAX, 0, screenWidth);
    int16_t y = map(p.x, X_MIN, X_MAX, 0, screenHeight);

    // Step 2: invert X only (CYD specific)
    x = screenWidth - x;

    // Clamp
    x = constrain(x, 0, screenWidth - 1);
    y = constrain(y, 0, screenHeight - 1);

    data->point.x = x;
    data->point.y = y;
    data->state   = LV_INDEV_STATE_PRESSED;
}

static void switch_toggle_event(lv_event_t *event){
    static bool switchState = false;
    switchState = !switchState;
    lv_obj_t *switchItem = lv_event_get_target_obj(event);
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *switch_widget = (lv_obj_t *)lv_event_get_user_data(event);
    lv_label_set_text(switch_widget, switchState? "ON":"OFF");
}


void setup()
{
    Serial.begin(115200);
    Serial.println("LVGL + EEZ + Touch starting");

    /* ---------- BACKLIGHT ---------- */
    pinMode(LCD_BACKLIGHT_PIN, OUTPUT);
    digitalWrite(LCD_BACKLIGHT_PIN, HIGH);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    /* ---------- DISPLAY ---------- */
    tft.init();
    tft.setRotation(3);
    tft.fillScreen(TFT_BLACK);

    /* ---------- TOUCH SPI INIT ---------- */
    SPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    touch.begin();               // <-- CORRECT
    touch.setRotation(0);        // MUST match display

    /* ---------- LVGL INIT ---------- */
    lv_init();

    lv_display_t *disp = lv_display_create(screenWidth, screenHeight);
    lv_display_set_buffers(
        disp,
        buf,
        NULL,
        sizeof(buf),
        LV_DISPLAY_RENDER_MODE_PARTIAL
    );
    lv_display_set_flush_cb(disp, my_disp_flush);

    /* ---------- REGISTER TOUCH (LVGL 9) ---------- */
    lv_indev_t *touch_indev = lv_indev_create();
    lv_indev_set_type(touch_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch_indev, my_touch_read);

    /* ---------- EEZ UI ---------- */
    ui_init();
    

    uart_slave_setup();
    uiManagerInit();
    simulatorInit();
}


void loop()
{
    lv_tick_inc(5);
    lv_timer_handler();
    delay(5);

    // Implementation to handle main screen displays

    if (simulatorUpdate(telemetry)) {
        uiManagerUpdate(telemetry);
    }
   

}
