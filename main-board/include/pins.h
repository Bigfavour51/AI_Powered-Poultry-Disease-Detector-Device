// pins.h - ESP32-S3-DevKitC-1 (N16R8) pin map for the main board.
//
// UARTs:
//   UART0  GPIO43 TX / GPIO44 RX   CH343 USB bridge ("UART" port): flashing + debug log (`Serial`)
//   UART1  GPIO17 TX / GPIO18 RX   Raspberry Pi (PDD protocol, pi_link)
//   UART2  GPIO41 TX / GPIO42 RX   CYD display (cyd_link)
//
// Do not use: GPIO0/3/45/46 (strapping), 19/20 (native USB), 26-32 (flash),
// 33-37 (octal PSRAM on the N16R8), 43/44 (UART0, already taken).
#pragma once
#include <Arduino.h>

// Raspberry Pi link (UART1). Pi GPIO14 TXD (pin 8) -> GPIO18, Pi GPIO15 RXD (pin 10) <- GPIO17,
// Pi GND (pin 6) -- GND. Both sides are 3.3 V logic.
#define PIN_PI_RX 18
#define PIN_PI_TX 17

// CYD display (UART2). S3 GPIO41 -> CYD GPIO35 (input-only, its RX), S3 GPIO42 <- CYD GPIO27,
// GND -- GND. CYD powered from the S3's 5V pin (its own regulator needs 5 V in).
#define PIN_CYD_RX 42
#define PIN_CYD_TX 41

// DHT11 temperature/humidity sensor data line (module with its own pull-up), powered from 3.3 V.
#define PIN_DHT 13

// MQ135 gas sensor module AO (ADC1). Module powered from 3.3 V, so AO never exceeds 3.3 V.
#define PIN_GAS 7

// Relay module IN, not used yet: held off. GPIO3 is a strapping pin, so it floats (and the relay
// may click once) during reset, until setup() drives it.
#define PIN_RELAY 3
#define RELAY_OFF HIGH  // low-level-trigger module: HIGH = off

// Onboard RGB LED (WS2812 on GPIO48; GPIO38 on DevKitC v1.1). The core drives it through
// digitalWrite(): HIGH = white, LOW = off.
#define PIN_LED LED_BUILTIN
