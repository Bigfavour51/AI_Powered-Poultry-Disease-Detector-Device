#ifndef EEZ_LVGL_UI_IMAGES_H
#define EEZ_LVGL_UI_IMAGES_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_img_dsc_t img_frame1;
extern const lv_img_dsc_t img_healthy_label;
extern const lv_img_dsc_t img_coccidiosis_label;
extern const lv_img_dsc_t img_newcastle_label;
extern const lv_img_dsc_t img_salmonella;
extern const lv_img_dsc_t img_charging_icon;

#ifndef EXT_IMG_DESC_T
#define EXT_IMG_DESC_T
typedef struct _ext_img_desc_t {
    const char *name;
    const lv_img_dsc_t *img_dsc;
} ext_img_desc_t;
#endif

extern const ext_img_desc_t images[6];

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_IMAGES_H*/