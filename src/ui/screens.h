#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Screens

enum ScreensEnum {
    _SCREEN_ID_FIRST = 1,
    SCREEN_ID_MAIN = 1,
    _SCREEN_ID_LAST = 1
};

typedef struct _objects_t {
    lv_obj_t *main;
    lv_obj_t *main_label_temperature;
    lv_obj_t *main_label_humidity;
    lv_obj_t *main_label_atmpressure;
    lv_obj_t *main_label_ammonia;
    lv_obj_t *main_label_batterylevel;
    lv_obj_t *main_img_healthy;
    lv_obj_t *main_img_cossidiosis;
    lv_obj_t *main_img_newcastle;
    lv_obj_t *main_img_salmonella;
    lv_obj_t *main_img_chargeicon;
    lv_obj_t *main_label_confidencelevel;
} objects_t;

extern objects_t objects;

void create_screen_main();
void tick_screen_main();

void tick_screen_by_id(enum ScreensEnum screenId);
void tick_screen(int screen_index);

void create_screens();

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/