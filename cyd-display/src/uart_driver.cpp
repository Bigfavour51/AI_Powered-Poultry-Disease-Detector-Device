#include "uart_driver.h"
#include "cyd_link.h"
#include "ui_manager.h"

// Photo of the last result (JPEG), assembled from IMG/IMD frames. Buffer on the heap (static
// DRAM is nearly full); nullptr if that failed, then photos are ignored.
static const size_t PHOTO_JPEG_MAX = 8192;
static cyd::ImageRx *photoRx;

static DiseaseType diseaseFromLabel(const char *label)
{
    if (strcmp(label, "HEALTHY") == 0)     return DiseaseType::HEALTHY;
    if (strcmp(label, "COCCIDIOSIS") == 0) return DiseaseType::COCCIDIOSIS;
    if (strcmp(label, "NEWCASTLE") == 0)   return DiseaseType::NEWCASTLE;
    if (strcmp(label, "SALMONELLA") == 0)  return DiseaseType::SALMONELLA;
    return DiseaseType::NONE;
}


void uart_slave_setup()
{
    // A photo arrives as ~90 frames back to back while LVGL may be busy redrawing
    Serial2.setRxBufferSize(4096);

    uint8_t *buf = (uint8_t *)malloc(PHOTO_JPEG_MAX);
    if (buf) photoRx = new cyd::ImageRx(buf, PHOTO_JPEG_MAX);

    Serial2.begin(
        BAUDRATE,
        SERIAL_8N1,
        RXPIN,
        TXPIN
    );

    Serial.println("UART SLAVE READY");
}

static bool handleLine(char *line, TelemetryData &data)
{
    char *f[16];
    int n = cyd::split(line, f, 16);

    if (n < 0)
    {
        Serial.println("Bad frame");
        return false;
    }

    if (strcmp(f[0], "TEL") == 0)
    {
        CydTelemetry t;

        if (!cyd::parseTel(f, n, t))
        {
            Serial.println("Bad TEL frame");
            return false;
        }

        data.temperature = t.temperature;
        data.humidity = t.humidity;
        data.pressure = t.pressure;
        data.ammonia = t.ammonia;
        data.batteryPercent = (int8_t)t.battery;
        data.charging = t.charging;
        data.disease = diseaseFromLabel(t.disease);
        data.confidence = t.confidence;
        data.resultSeq = t.seq;
        data.linkOk = true;
        data.piAlive = t.piAlive;
        data.cameraOk = t.cameraOk;
        data.modelOk = t.modelOk;
        data.busy = t.busy;
        data.uptime = t.uptime;
        return true;
    }

    if (photoRx && photoRx->feed(f, n))
    {
        Serial.printf("Photo #%u received (%ux%u, %u bytes)\r\n", photoRx->seq, photoRx->w,
                      photoRx->h, (unsigned)photoRx->len());
        uiManagerShowPhoto(photoRx->data(), photoRx->len(), photoRx->seq);
    }

    return false;
}

bool uart_slave_read(TelemetryData &data)
{
    static char line[cyd::FRAME_MAX];
    static size_t len = 0;

    bool newData = false;

    while (Serial2.available())
    {
        char c = Serial2.read();

        if (c == '\r')
        {
            continue;
        }

        if (c != '\n')
        {
            // Overlong line: drop it
            if (len < sizeof(line) - 1) line[len++] = c;
            else len = 0;
            continue;
        }

        line[len] = '\0';

        if (len > 0 && handleLine(line, data))
        {
            newData = true;
        }

        len = 0;
    }

    return newData;
}
