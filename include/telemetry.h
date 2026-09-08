#pragma once
#include <Arduino.h>

enum class DiseaseType : uint8_t
{
    HEALTHY = 0,
    COCCIDIOSIS,
    NEWCASTLE,
    SALMONELLA
};

struct TelemetryData
{
    float temperature;
    float humidity;
    float pressure;
    float ammonia;

    uint8_t batteryPercent;
    bool charging;

    DiseaseType disease;
    uint8_t confidence;
};