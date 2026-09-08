#ifndef UART_DRIVER_H
#define UART_DRIVER_H

#include <Arduino.h>

#define TXPIN       27
#define RXPIN       22
#define BAUDRATE    9600

struct TelemetryData
{
    float temperature;
    int32_t batteryPercent;
    float humidity;
    float pressure;
    float ammonia;
    bool isCharging;
};

extern TelemetryData telemetry;

void uart_slave_setup();
bool uart_slave_read();

#endif