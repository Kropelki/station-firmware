#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_AHTX0.h>
#include <BH1750.h>
#include <math.h>

#include "env.h"

const float voltage_multiplier = 3.2;

#define BMP280_AHT20_PIN 12
#define BH1750_PIN 14
#define SOLAR_PANEL_VOLTAGE_PIN 35
#define BATTERY_VOLTAGE_PIN 34


// Sensor instances
Adafruit_BMP280 bmp;
Adafruit_AHTX0 aht;
BH1750 lightMeter;

float calculateDewPoint(float tempC, float humidity) {
  const float a = 17.62;
  const float b = 243.12;
  float alpha = ((a * tempC) / (b + tempC)) + log(humidity / 100.0);
  return (b * alpha) / (a - alpha);
}

void prepare_gpio_for_sleep() {
  for (int pin = 0; pin <= 39; pin++) {
    if (pin != 6 && pin != 7 && pin != 8 && pin != 9 && pin != 10 && pin != 11 && pin != 1 && pin != 3 && pin != 34 && pin != 35) {
      pinMode(pin, PULLDOWN);
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

void sendToDatabase(float tempC, float humidity, float pressure_hPa, float dewpointC, float lux, float battery_voltage, float solar_panel_voltage) {
  HTTPClient http;
  String url = String(TEST_SERVER_HOST) + ":" + String(TEST_SERVER_PORT) + "/api/weather" +
               "?temperature=" + String(tempC, 2) +
               "&dew_point=" + String(dewpointC, 2) +
               "&humidity=" + String(humidity, 1) +
               "&illumination=" + String(lux, 1) +
               "&pressure=" + String(pressure_hPa, 2) +
               "&battery_voltage=" + String(battery_voltage, 2) +
               "&solar_panel_voltage=" + String(solar_panel_voltage, 2);

  Serial.println("Sending to: " + url);
  http.begin(url);
  int httpCode = http.GET();
  if (httpCode > 0) {
    Serial.println("Response: " + http.getString());
  }
  else {
    Serial.println("Error on sending request");
  }
  http.end();
}

void sendToInfluxDB(float tempC, float humidity, float pressure_hPa, float dewpointC, float lux, float battery_voltage, float solar_panel_voltage) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;

    const char* influxdb_url = "https://eu-central-1-1.aws.cloud2.influxdata.com/api/v2/write?bucket=db.v0&precision=ns";
    
    unsigned long start = millis();
    Serial.print("Sending data to InfluxDB...");

    unsigned long dns_start = millis();
    http.begin(influxdb_url);
    Serial.printf("HTTP begin took: %lu ms\n", millis() - dns_start);
    http.setTimeout(10000);
    
    http.addHeader("Authorization", String("Token ") + INFLUXDB_API_TOKEN);
    http.addHeader("Content-Type", "text/plain; charset=utf-8");
    http.addHeader("Accept", "application/json");
    // example_payload = "weather temperature=200.37,humidity=40.2,pressure=1012,illumination=56.3,dew_point=30 1750260267000000000";

    String payload = String("weather ") +
                     "temperature=" + String(tempC, 2) + "," +
                     "humidity=" + String(humidity, 1) + "," +
                     "pressure=" + String(pressure_hPa, 2) + "," +
                     "illumination=" + String(lux, 1) + "," +
                     "dew_point=" + String(dewpointC, 1) + "," +
                     "battery_voltage=" + String(battery_voltage, 2) + "," +
                     "solar_panel_voltage=" + String(solar_panel_voltage, 2);
    
    unsigned long post_start = millis();
    int httpResponseCode = http.POST(payload);
    Serial.println(payload);
    Serial.printf("POST took: %lu ms\n", millis() - post_start);
    Serial.printf("Total time: %lu ms\n", millis() - start);
    
    if (httpResponseCode > 0) {
      // String response = http.getString();
      Serial.print("HTTP Response Code: ");
      Serial.println(httpResponseCode);
      // Serial.print("Response: ");
      // Serial.println(response);
    } else {
      Serial.print("Error in HTTP request: ");
      Serial.println(httpResponseCode);
    }
    
    http.end();
  } else {
    Serial.println("WiFi not connected");
  }
}

void setup() {
  btStop();

  pinMode(BMP280_AHT20_PIN, OUTPUT);
  pinMode(BH1750_PIN, OUTPUT);
  digitalWrite(BMP280_AHT20_PIN, HIGH);
  digitalWrite(BH1750_PIN, HIGH);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  Serial.begin(115200);
  delay(1000);

  Wire.begin(21, 22); // SDA, SCL
  connectToWiFi();

  if (!bmp.begin(0x77)) {
    Serial.println("Could not find BMP280!");
    while (1) delay(10);
  }

  if (!aht.begin()) {
    Serial.println("Could not find AHT20!");
    while (1) delay(10);
  }

  if (!lightMeter.begin()) {
    Serial.println("Could not find BH1750!");
    while (1) delay(10);
  }

  int raw = analogRead(SOLAR_PANEL_VOLTAGE_PIN);
  float solar_panel_voltage = (raw / 4095.0) * 3.3 * voltage_multiplier;
  raw = analogRead(BATTERY_VOLTAGE_PIN);
  float battery_voltage = (raw / 4095.0) * 3.3 * voltage_multiplier;

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
  Serial.printf("Battery voltage: %.2f V\n", battery_voltage);
  Serial.printf("Solar panel voltage: %.2f V\n", solar_panel_voltage);

  // sendToDatabase(temp.temperature, humidity.relative_humidity, pressure, dewpoint, lux, battery_voltage, solar_panel_voltage);
  try {
    sendToInfluxDB(temp.temperature, humidity.relative_humidity, pressure, dewpoint, lux, battery_voltage, solar_panel_voltage);
  } catch (const std::exception& e) {
    Serial.printf("Error sending data to InfluxDB: %s\n", e.what());
  } catch (...) {
    Serial.println("Unknown error occurred while sending data to InfluxDB");
  }

  // Deep sleep for 5 minutes (300,000,000 µs)
  Serial.println("Entering deep sleep for 5 minutes...");


  prepare_gpio_for_sleep();
  WiFi.mode(WIFI_OFF);
  // esp_sleep_enable_timer_wakeup(1000000);
  esp_sleep_enable_timer_wakeup(300000000);
  esp_deep_sleep_start();
}

void loop() {
  // Empty, everything is done in setup()
}

