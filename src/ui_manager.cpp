#include <Arduino.h>
#include <lvgl.h>

#include "ui_manager.h"
#include "ui/screens.h"

static void updateDiseaseImage(DiseaseType disease)
{
    // Hide every disease image first
    lv_obj_add_flag(objects.main_img_healthy, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(objects.main_img_cossidiosis, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(objects.main_img_newcastle, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(objects.main_img_salmonella, LV_OBJ_FLAG_HIDDEN);

    // Show only the current result
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
    }
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
    // Optional initialization.
}


void uiManagerUpdate(const TelemetryData &data)
{
    char buffer[32];

    // Temperature
    snprintf(
        buffer,
        sizeof(buffer),
        "%.1f",
        data.temperature
    );

    lv_label_set_text(
        objects.main_label_temperature,
        buffer
    );


    // Humidity
    snprintf(
        buffer,
        sizeof(buffer),
        "%.1f",
        data.humidity
    );

    lv_label_set_text(
        objects.main_label_humidity,
        buffer
    );


    // Atmospheric pressure
    snprintf(
        buffer,
        sizeof(buffer),
        "%.1f",
        data.pressure
    );

    lv_label_set_text(
        objects.main_label_atmpressure,
        buffer
    );


    // Ammonia
    snprintf(
        buffer,
        sizeof(buffer),
        "%.1f",
        data.ammonia
    );

    lv_label_set_text(
        objects.main_label_ammonia,
        buffer
    );


    // Battery
    snprintf(
        buffer,
        sizeof(buffer),
        "%u",
        data.batteryPercent
    );

    lv_label_set_text(
        objects.main_label_batterylevel,
        buffer
    );


    // AI confidence
    snprintf(
        buffer,
        sizeof(buffer),
        "%u",
        data.confidence
    );

    lv_label_set_text(
        objects.main_label_confidencelevel,
        buffer
    );


    // Disease result
    updateDiseaseImage(data.disease);

    // Charging state
    updateChargingIcon(data.charging);
}