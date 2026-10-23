#pragma once

#include "telemetry.h"

void uiManagerInit();
void uiManagerUpdate(const TelemetryData &data);

// Decodes a baseline JPEG and shows it in the camera panel (photo of result seq)
void uiManagerShowPhoto(const uint8_t *jpeg, size_t len, uint16_t seq);
