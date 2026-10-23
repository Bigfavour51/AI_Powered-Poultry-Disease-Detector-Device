#pragma once
#include <Arduino.h>
#include <math.h>

enum class DiseaseType : uint8_t
{
    HEALTHY = 0,
    COCCIDIOSIS,
    NEWCASTLE,
    SALMONELLA,
    NONE            // no result yet: no disease image shown
};

// NAN / -1 / NONE mean "no value" and are shown as "--"
struct TelemetryData
{
    float temperature = NAN;
    float humidity = NAN;
    float pressure = NAN;
    float ammonia = NAN;

    int8_t batteryPercent = -1;
    bool charging = false;

    DiseaseType disease = DiseaseType::NONE;
    uint8_t confidence = 0;
    uint16_t resultSeq = 0;     // Pi result number the disease belongs to

    // System status (status dots)
    bool linkOk = false;        // frames arriving from the S3 main board
    bool piAlive = false;
    bool cameraOk = false;
    bool modelOk = false;
    bool busy = false;          // capture/inference running on the Pi
    uint32_t uptime = 0;        // main board, s
};
