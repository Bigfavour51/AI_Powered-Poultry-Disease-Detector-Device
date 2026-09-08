#include "uart_driver.h"

TelemetryData telemetry = {
    0.0f,
    0,
    0.0f,
    0.0f,
    0.0f,
    false
};


void uart_slave_setup()
{
    Serial.begin(115200);

    Serial2.begin(
        BAUDRATE,
        SERIAL_8N1,
        RXPIN,
        TXPIN
    );

    delay(1000);

    Serial.println("UART SLAVE READY");
}

bool uart_slave_read()
{
    static String input = "";

    bool newData = false;

    while (Serial2.available())
    {
        char c = Serial2.read();

        if (c == '\n')
        {
            input.trim();

            if (input.length() == 0)
            {
                continue;
            }

            Serial.print("Received: ");
            Serial.println(input);

            // Remove '#'
            if (input.charAt(0) == '#')
            {
                input.remove(0, 1);
            }

            char buf[128];

            input.toCharArray(buf, sizeof(buf));

            char *token = strtok(buf, ",");

            float values[6];
            int idx = 0;

            while (token != nullptr && idx < 6)
            {
                values[idx] = atof(token);

                idx++;

                token = strtok(nullptr, ",");
            }

            if (idx == 6)
            {
                telemetry.temperature =
                    values[0];

                telemetry.batteryPercent =
                    (int32_t)values[1];

                telemetry.humidity =
                    values[2];

                telemetry.pressure =
                    values[3];

                telemetry.ammonia =
                    values[4];

                telemetry.isCharging =
                    (values[5] != 0);

                newData = true;
            }

            input = "";
        }
        else
        {
            input += c;

            // Prevent runaway buffer
            if (input.length() >= 127)
            {
                input = "";
            }
        }
    }

    return newData;
}