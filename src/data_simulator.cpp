#include <Arduino.h>
#include "data_simulator.h"

static unsigned long previousUpdate = 0;

void simulatorInit()
{
    randomSeed(esp_random());
}


bool simulatorUpdate(TelemetryData &data)
{
    // Update once every 2 seconds
    if (millis() - previousUpdate < 2000)
    {
        return false;
    }

    previousUpdate = millis();

    // DHT22
    data.temperature = random(240, 360) / 10.0f;
    data.humidity    = random(450, 850) / 10.0f;

    // BME280
    data.pressure = random(9900, 10300) / 10.0f;

    // MQ135 / NH3
    data.ammonia = random(0, 300) / 10.0f;

    // Battery
    data.batteryPercent = random(20, 101);

    // Random charging state
    data.charging = random(0, 2);

    // Random AI result
    data.disease =
        static_cast<DiseaseType>(random(0, 4));

    // AI confidence
    data.confidence = random(70, 100);

    return true;
}