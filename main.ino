#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_AHTX0.h>
#include <BH1750.h>
#include <math.h>

#include "env.h"

#define BMP280_AHT20_PIN 12
#define BH1750_PIN 14

// Sensor instances
Adafruit_BMP280 bmp; // Adres 0x77 domyślnie
Adafruit_AHTX0 aht;
BH1750 lightMeter;

float calculateDewPoint(float tempC, float humidity)
{
    const float a = 17.62;
    const float b = 243.12;
    float alpha = ((a * tempC) / (b + tempC)) + log(humidity / 100.0);
    return (b * alpha) / (a - alpha);
}

void prepare_gpio_for_sleep()
{
    for (int pin = 0; pin <= 39; pin++)
    {
        if (pin != 6 && pin != 7 && pin != 8 && pin != 9 && pin != 10 && pin != 11 && pin != 1 && pin != 3)
        {
            pinMode(pin, PULLDOWN); // lub OUTPUT LOW
        }
    }
}

void connectToWiFi()
{
    Serial.print("Connecting to WiFi...");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected!");
    Serial.println(WiFi.localIP());
}

void sendToDatabase(float tempC, float humidity, float pressure_hPa, float dewpointC, float lux)
{
    HTTPClient http;
    String url = "http://192.168.1.18:8000/api/weather?temperature=" + String(tempC, 2) +
                 "&dew_point=" + String(dewpointC, 2) +
                 "&humidity=" + String(humidity, 1) +
                 "&illumination=" + String(lux, 1) +
                 "&pressure=" + String(pressure_hPa, 2);

    Serial.println("Sending to: " + url);
    http.begin(url);
    int httpCode = http.GET();
    if (httpCode > 0)
    {
        Serial.println("Response: " + http.getString());
    }
    else
    {
        Serial.println("Error on sending request");
    }
    http.end();
}

void setup()
{
    btStop();

    pinMode(BMP280_AHT20_PIN, OUTPUT);
    pinMode(BH1750_PIN, OUTPUT);
    digitalWrite(BMP280_AHT20_PIN, HIGH);
    digitalWrite(BH1750_PIN, HIGH);

    Serial.begin(115200);
    delay(1000);

    Wire.begin(21, 22); // SDA, SCL
    connectToWiFi();

    if (!bmp.begin(0x77))
    {
        Serial.println("Could not find BMP280!");
        while (1)
            delay(10);
    }

    if (!aht.begin())
    {
        Serial.println("Could not find AHT20!");
        while (1)
            delay(10);
    }

    if (!lightMeter.begin())
    {
        Serial.println("Could not find BH1750!");
        while (1)
            delay(10);
    }

    sensors_event_t humidity, temp;
    aht.getEvent(&humidity, &temp);
    float pressure = bmp.readPressure() / 100.0;
    float baromin = pressure * 0.02953;
    float lux = lightMeter.readLightLevel();
    float dewpoint = calculateDewPoint(temp.temperature, humidity.relative_humidity);

    Serial.printf("Temperature: %.2f °C\n", temp.temperature);
    Serial.printf("Humidity: %.1f %%\n", humidity.relative_humidity);
    Serial.printf("Pressure: %.2f hPa\n", pressure);
    Serial.printf("Baromin: %.2f inHg\n", baromin);
    Serial.printf("Dew Point: %.2f °C\n", dewpoint);
    Serial.printf("Illumination: %.1f lx\n", lux);

    sendToDatabase(temp.temperature, humidity.relative_humidity, pressure, dewpoint, lux);

    // Deep sleep for 5 minutes (300,000,000 µs)
    Serial.println("Entering deep sleep for 5 minutes...");

    prepare_gpio_for_sleep();
    WiFi.mode(WIFI_OFF);
    esp_sleep_enable_timer_wakeup(300000000);
    esp_deep_sleep_start();
}

void loop()
{
    // Empty, everything is done in setup()
}
