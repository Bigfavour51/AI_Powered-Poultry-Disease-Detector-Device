#ifndef UART_DRIVER_H
#define UART_DRIVER_H

#include <Arduino.h>
#include "telemetry.h"

// Link to the ESP32-S3 main board (frame format in lib/CydLink/cyd_link.h)
#define RXPIN       35      // <- S3 GPIO41 (input-only pin, fine for RX)
#define TXPIN       27      // -> S3 GPIO42
#define BAUDRATE    115200

void uart_slave_setup();

// Returns true when a valid frame arrived; data then holds the new values.
bool uart_slave_read(TelemetryData &data);

#endif
