#include <Arduino.h>
#include <lvgl.h>

#include "ui_manager.h"
#include "ui/screens.h"
#include "src/libs/tjpgd/tjpgd.h"

/* ================= CAMERA PANEL (photo) ================= */
// Inside of the camera panel drawn in the background image
#define PHOTO_BOX_X  104
#define PHOTO_BOX_Y  34
#define PHOTO_BOX_W  118
#define PHOTO_BOX_H  86
#define PHOTO_MAX_W  120
#define PHOTO_MAX_H  90

// Heap, not static: the classic ESP32's static DRAM is already nearly full
static uint16_t *photoPixels;     // PHOTO_MAX_W x PHOTO_MAX_H, RGB565
static uint8_t *jpegWork;         // TJpgDec work area
static const size_t JPEG_WORK_SIZE = 4096;
static lv_image_dsc_t photoDsc;
static lv_obj_t *photoImg;
static lv_obj_t *busyLabel;
static uint16_t photoSeq = 0;       // result the photo shown belongs to

/* ================= SYSTEM STATUS / UPTIME ================= */
// Dots over the ones drawn in the background: MCU, Raspberry Pi, camera, UART MCU>Pi, AI model
static const int16_t STATUS_DOT_X = 301;
static const int16_t STATUS_DOT_Y[5] = {61, 77, 96, 112, 131};
static lv_obj_t *statusDots[5];
static lv_obj_t *uptimeLabel;

static lv_obj_t *makeLabel(int16_t x, int16_t y, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *obj = lv_label_create(objects.main);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_text_font(obj, font, LV_PART_MAIN);
    lv_label_set_text(obj, "");
    return obj;
}

static void updateDiseaseImage(DiseaseType disease)
{
    // Hide every disease image first
    lv_obj_add_flag(objects.main_img_healthy, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(objects.main_img_cossidiosis, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(objects.main_img_newcastle, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(objects.main_img_salmonella, LV_OBJ_FLAG_HIDDEN);

    // Show only the current result (none when there is no result yet)
    switch (disease)
    {
        case DiseaseType::HEALTHY:
            lv_obj_remove_flag(
                objects.main_img_healthy,
                LV_OBJ_FLAG_HIDDEN
            );
            break;

        case DiseaseType::COCCIDIOSIS:
            lv_obj_remove_flag(
                objects.main_img_cossidiosis,
                LV_OBJ_FLAG_HIDDEN
            );
            break;

        case DiseaseType::NEWCASTLE:
            lv_obj_remove_flag(
                objects.main_img_newcastle,
                LV_OBJ_FLAG_HIDDEN
            );
            break;

        case DiseaseType::SALMONELLA:
            lv_obj_remove_flag(
                objects.main_img_salmonella,
                LV_OBJ_FLAG_HIDDEN
            );
            break;

        case DiseaseType::NONE:
            break;
    }
}


// Sensor value with one decimal, or "--" when there is no reading
static void setValueLabel(lv_obj_t *label, float value)
{
    char buffer[16];

    if (isnan(value))
    {
        lv_label_set_text(label, "--");
        return;
    }

    snprintf(buffer, sizeof(buffer), "%.1f", value);
    lv_label_set_text(label, buffer);
}


static void updateChargingIcon(bool charging)
{
    if (charging)
    {
        lv_obj_remove_flag(
            objects.main_img_chargeicon,
            LV_OBJ_FLAG_HIDDEN
        );
    }
    else
    {
        lv_obj_add_flag(
            objects.main_img_chargeicon,
            LV_OBJ_FLAG_HIDDEN
        );
    }
}


void uiManagerInit()
{
    photoPixels = (uint16_t *)malloc(PHOTO_MAX_W * PHOTO_MAX_H * 2);
    jpegWork = (uint8_t *)malloc(JPEG_WORK_SIZE);

    for (int i = 0; i < 5; i++)
    {
        lv_obj_t *dot = lv_obj_create(objects.main);
        lv_obj_remove_style_all(dot);
        lv_obj_set_pos(dot, STATUS_DOT_X, STATUS_DOT_Y[i] - 4);
        lv_obj_set_size(dot, 9, 9);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(dot, lv_color_hex(0x808080), LV_PART_MAIN);
        statusDots[i] = dot;
    }

    uptimeLabel = makeLabel(50, 219, &lv_font_montserrat_10, 0xffffff);

    photoImg = lv_image_create(objects.main);
    lv_obj_add_flag(photoImg, LV_OBJ_FLAG_HIDDEN);

    busyLabel = makeLabel(0, 0, &lv_font_montserrat_12, 0x7cfc00);
    lv_label_set_text(busyLabel, "CAPTURING...");
    lv_obj_set_style_bg_color(busyLabel, lv_color_hex(0x0b161f), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(busyLabel, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_pad_all(busyLabel, 4, LV_PART_MAIN);
    lv_obj_update_layout(busyLabel);
    lv_obj_set_pos(busyLabel, PHOTO_BOX_X + (PHOTO_BOX_W - lv_obj_get_width(busyLabel)) / 2,
                   PHOTO_BOX_Y + (PHOTO_BOX_H - lv_obj_get_height(busyLabel)) / 2);
    lv_obj_add_flag(busyLabel, LV_OBJ_FLAG_HIDDEN);
}


/* ---------- JPEG -> RGB565 with TJpgDec (bundled with LVGL) ---------- */
struct JpegIo
{
    const uint8_t *src;
    size_t len;
    size_t pos;
    uint16_t w;
    uint16_t h;
};

static size_t jpegIn(JDEC *jd, uint8_t *buf, size_t n)
{
    JpegIo *io = (JpegIo *)jd->device;
    n = min(n, io->len - io->pos);
    if (buf) memcpy(buf, io->src + io->pos, n);
    io->pos += n;
    return n;
}

static int jpegOut(JDEC *jd, void *bitmap, JRECT *r)
{
    JpegIo *io = (JpegIo *)jd->device;
    const uint8_t *p = (const uint8_t *)bitmap;     // RGB888

    for (int y = r->top; y <= r->bottom; y++)
    {
        for (int x = r->left; x <= r->right; x++, p += 3)
        {
            if (x < io->w && y < io->h)
            {
                photoPixels[y * io->w + x] =
                    ((p[0] & 0xF8) << 8) | ((p[1] & 0xFC) << 3) | (p[2] >> 3);
            }
        }
    }

    return 1;
}

void uiManagerShowPhoto(const uint8_t *jpeg, size_t len, uint16_t seq)
{
    if (!photoPixels || !jpegWork)
    {
        Serial.println("Photo not shown (no memory)");
        return;
    }

    JpegIo io = {jpeg, len, 0, 0, 0};
    JDEC jd;

    JRESULT res = jd_prepare(&jd, jpegIn, jpegWork, JPEG_WORK_SIZE, &io);

    if (res != JDR_OK || jd.width > PHOTO_MAX_W || jd.height > PHOTO_MAX_H)
    {
        Serial.printf("Photo not shown (jd_prepare %d, %ux%u)\r\n", res, jd.width, jd.height);
        return;
    }

    io.w = jd.width;
    io.h = jd.height;
    res = jd_decomp(&jd, jpegOut, 0);

    if (res != JDR_OK)
    {
        Serial.printf("Photo not shown (jd_decomp %d)\r\n", res);
        return;
    }

    memset(&photoDsc, 0, sizeof(photoDsc));
    photoDsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    photoDsc.header.cf = LV_COLOR_FORMAT_RGB565;
    photoDsc.header.w = io.w;
    photoDsc.header.h = io.h;
    photoDsc.header.stride = io.w * 2;
    photoDsc.data_size = io.w * io.h * 2;
    photoDsc.data = (const uint8_t *)photoPixels;

    lv_image_set_src(photoImg, NULL);
    lv_image_set_src(photoImg, &photoDsc);
    lv_obj_set_pos(photoImg, PHOTO_BOX_X + (PHOTO_BOX_W - io.w) / 2,
                   PHOTO_BOX_Y + (PHOTO_BOX_H - io.h) / 2);
    lv_obj_remove_flag(photoImg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(photoImg);
    photoSeq = seq;
}


static void setStatusDot(int i, bool ok)
{
    lv_obj_set_style_bg_color(statusDots[i], lv_color_hex(ok ? 0x22c55e : 0xef4444), LV_PART_MAIN);
}


void uiManagerUpdate(const TelemetryData &data)
{
    char buffer[32];

    setValueLabel(objects.main_label_temperature, data.temperature);
    setValueLabel(objects.main_label_humidity, data.humidity);
    setValueLabel(objects.main_label_atmpressure, data.pressure);
    setValueLabel(objects.main_label_ammonia, data.ammonia);


    // Battery
    if (data.batteryPercent < 0)
    {
        lv_label_set_text(objects.main_label_batterylevel, "--");
    }
    else
    {
        snprintf(buffer, sizeof(buffer), "%d", data.batteryPercent);
        lv_label_set_text(objects.main_label_batterylevel, buffer);
    }


    // AI confidence
    if (data.disease == DiseaseType::NONE)
    {
        lv_label_set_text(objects.main_label_confidencelevel, "--");
    }
    else
    {
        snprintf(buffer, sizeof(buffer), "%u", data.confidence);
        lv_label_set_text(objects.main_label_confidencelevel, buffer);
    }


    // Disease result
    updateDiseaseImage(data.disease);

    // Charging state
    updateChargingIcon(data.charging);


    // Photo: hidden while a capture runs, and once a newer result than the photo arrives
    if (data.busy || data.resultSeq != photoSeq)
    {
        lv_obj_add_flag(photoImg, LV_OBJ_FLAG_HIDDEN);
    }

    if (data.busy)
    {
        lv_obj_remove_flag(busyLabel, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(busyLabel, LV_OBJ_FLAG_HIDDEN);
    }


    // System status
    setStatusDot(0, data.linkOk);
    setStatusDot(1, data.piAlive);
    setStatusDot(2, data.piAlive && data.cameraOk);
    setStatusDot(3, data.piAlive);
    setStatusDot(4, data.piAlive && data.modelOk);


    // Main-board uptime
    if (data.linkOk)
    {
        snprintf(buffer, sizeof(buffer), "%02lu:%02lu:%02lu", (unsigned long)(data.uptime / 3600),
                 (unsigned long)(data.uptime / 60 % 60), (unsigned long)(data.uptime % 60));
        lv_label_set_text(uptimeLabel, buffer);
    }
    else
    {
        lv_label_set_text(uptimeLabel, "--");
    }
}
