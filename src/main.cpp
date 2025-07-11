#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <Wire.h>
#include <Adafruit_BMP280.h>
#include <Adafruit_AHTX0.h>
#include <BH1750.h>
#include <math.h>
#include "driver/rtc_io.h"

#include "env.h"

const float voltage_multiplier = 3.2;
String logBuffer = "";

#define BMP280_AHT20_PIN 12
#define BH1750_PIN 14
#define SOLAR_PANEL_VOLTAGE_PIN 35
#define BATTERY_VOLTAGE_PIN 34


// Sensor instances
Adafruit_BMP280 bmp;
Adafruit_AHTX0 aht;
BH1750 lightMeter;

void serialLog(String message) {
  logBuffer += message + "\n";
  Serial.println(message);
}

float calculateDewPoint(float temperature_c, float humidity) {
  const float a = 17.62;
  const float b = 243.12;
  float alpha = ((a * temperature_c) / (b + temperature_c)) + log(humidity / 100.0);
  return(b * alpha) / (a - alpha);
}

void isolate_all_rtc_gpio() {
    const gpio_num_t rtc_gpio_list[] = {
        GPIO_NUM_0,
        GPIO_NUM_2,
        GPIO_NUM_4,
        GPIO_NUM_12,
        GPIO_NUM_13,
        GPIO_NUM_14,
        GPIO_NUM_15,
        GPIO_NUM_25,
        GPIO_NUM_26,
        GPIO_NUM_27,
        GPIO_NUM_32,
        GPIO_NUM_33,
        GPIO_NUM_34,
        GPIO_NUM_35,
        GPIO_NUM_36,
        GPIO_NUM_37,
        GPIO_NUM_38,
        GPIO_NUM_39
    };

    for (int i = 0; i < sizeof(rtc_gpio_list)/sizeof(rtc_gpio_list[0]); i++) {
        rtc_gpio_isolate(rtc_gpio_list[i]);
    }
}

void connectToWiFi() {
    serialLog("Connecting to WiFi...");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while(WiFi.status() != WL_CONNECTED) {
      delay(500);
      serialLog(".");
    }
    serialLog("\nWiFi connected!");
    serialLog(WiFi.localIP().toString());
}

void sendLog() {
  WiFiClient client;
  if (client.connect(LOG_SERVER_HOST, LOG_SERVER_PORT)) {
    int contentLength = logBuffer.length();
    client.print(String("POST ") + LOG_SERVER_PATH + " HTTP/1.1\r\n" +
                 "Host: " + LOG_SERVER_HOST + "\r\n" +
                 "Content-Type: text/plain\r\n" +
                 "Content-Length: " + String(contentLength) + "\r\n" +
                 "Connection: close\r\n\r\n" +
                 logBuffer);

    delay(10);
    client.stop();

    serialLog("Log sent synchronously (no response expected).");
  } else {
    serialLog("Failed to connect to the log server.");
  }
}

void sendToDatabase(float temperature_c, float humidity, float pressure, float dewpoint_c, float illumination, float battery_voltage, float solar_panel_voltage) {
  HTTPClient http;
  String url = String(TEST_SERVER_HOST) + ":" + String(TEST_SERVER_PORT) + "/api/weather" +
               "?temperature=" + String(temperature_c, 2) +
               "&dew_point=" + String(dewpoint_c, 2) +
               "&humidity=" + String(humidity, 1) +
               "&illumination=" + String(illumination, 1) +
               "&pressure=" + String(pressure, 2) +
               "&battery_voltage=" + String(battery_voltage, 2) +
               "&solar_panel_voltage=" + String(solar_panel_voltage, 2);

  serialLog("Sending to: " + url);
  http.begin(url);
  int httpCode = http.GET();
  if(httpCode > 0) {
    serialLog("Response: " + http.getString());
  }
  else {
    serialLog("Error on sending request");
  }
  http.end();
}

void sendToInfluxDB(float temperature_c, float humidity, float pressure, float dewpoint_c, float illumination, float battery_voltage, float solar_panel_voltage) {
  if(WiFi.status() == WL_CONNECTED) {
    HTTPClient http;

    const char* InfluxDB_url = "https://eu-central-1-1.aws.cloud2.influxdata.com/api/v2/write?bucket=db.v0&precision=ns";

    serialLog("Sending data to InfluxDB...");

    http.begin(InfluxDB_url);
    http.setTimeout(10000);

    http.addHeader("Authorization", String("Token ") + INFLUXDB_API_TOKEN);
    http.addHeader("Content-Type", "text/plain; charset=utf-8");
    http.addHeader("Accept", "application/json");

    // String payload = "weather temperature=200.37,humidity=40.2,pressure=1012,illumination=56.3,dew_point=30";
    String payload = String("weather ") +
                     "temperature=" + String(temperature_c, 2) + "," +
                     "humidity=" + String(humidity, 1) + "," +
                     "pressure=" + String(pressure, 2) + "," +
                     "illumination=" + String(illumination, 1) + "," +
                     "dew_point=" + String(dewpoint_c, 1) + "," +
                     "battery_voltage=" + String(battery_voltage, 2) + "," +
                     "solar_panel_voltage=" + String(solar_panel_voltage, 2);

    int httpResponseCode = http.POST(payload);
    serialLog(payload);

    if(httpResponseCode > 0) {
      // String response = http.getString();
      serialLog("HTTP Response Code: ");
      serialLog(String(httpResponseCode));
      // serialLog("Response: ");
      // serialLog(response);
    }
    else {
      serialLog("Error in HTTP request: ");
      serialLog(String(httpResponseCode));
      // serialLog(http.getString());
    }

    http.end();
  }
  else {
    serialLog("WiFi not connected");
  }
}

void sendToWeatherUnderground(float temperature_f, int humidity, float baromin, float dewpoint_f) {
  if(WiFi.status() == WL_CONNECTED) {
    String url = "http://weatherstation.wunderground.com/weatherstation/updateweatherstation.php";
    url += "?ID=" + String(WEATHER_UNDERGROUND_STATION_ID);
    url += "&PASSWORD=" + String(WEATHER_UNDERGROUND_API_KEY);
    url += "&dateutc=now";
    url += "&tempf=" + String(temperature_f, 2);
    url += "&dewptf=" + String(dewpoint_f, 2);
    url += "&humidity=" + String(humidity);
    url += "&baromin=" + String(baromin, 2);
    url += "&action=updateraw";

    Serial.println("Sending data: " + url);

    HTTPClient http;
    http.begin(url);
    int httpCode = http.GET();

    if(httpCode > 0) {
      String payload = http.getString();
      Serial.println("Response: " + payload);
    }
    else {
      Serial.println("Sending error: " + http.errorToString(httpCode));
    }

    http.end();
  }
  else {
    serialLog("WiFi not connected");
  }
}

void setup() {
  unsigned long startTime = millis();
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

  float illumination = 0;
  float temperature_c = -1000;
  float humidity = -1000;
  float pressure = -1000;

  if(bmp.begin(0x77)) {
    pressure = bmp.readPressure() / 100.0;
  }
  else {
    serialLog("Could not find BMP280!");
  }

  if(aht.begin()) {
    sensors_event_t hum, temp;
    aht.getEvent(&hum, &temp);
    temperature_c = temp.temperature;
    humidity = hum.relative_humidity;
  }
  else {
    serialLog("Could not find AHT20!");
  }

  if(lightMeter.begin()) {
    float illumination = lightMeter.readLightLevel();
  }
  else {
    serialLog("Could not find BH1750!");
  }

  int raw = analogRead(SOLAR_PANEL_VOLTAGE_PIN);
  float solar_panel_voltage = (raw / 4095.0) * 3.3 * voltage_multiplier;
  raw = analogRead(BATTERY_VOLTAGE_PIN);
  float battery_voltage = (raw / 4095.0) * 3.3 * voltage_multiplier;

  float temperature_f = temperature_c * 9.0 / 5.0 + 32.0;
  float baromin = pressure * 0.02953;
  float dewpoint_c = calculateDewPoint(temperature_c, humidity);
  float dewpoint_f = dewpoint_c * 9.0 / 5.0 + 32.0;

  serialLog("Temperature: " + String(temperature_c, 2) + " °C (" + String(temperature_f, 2) + " °F)");
  serialLog("Humidity: " + String(humidity, 1) + " %");
  serialLog("Pressure: " + String(pressure, 2) + " hPa");
  serialLog("Baromin: " + String(baromin, 2) + " inHg");
  serialLog("Dew Point: " + String(dewpoint_c, 2) + " °C (" + String(dewpoint_f, 2) + " °F)");
  serialLog("Illumination: " + String(illumination, 1) + " lx");
  serialLog("Battery voltage: " + String(battery_voltage, 2) + " V");
  serialLog("Solar panel voltage: " + String(solar_panel_voltage, 2) + " V");


  unsigned long activeTime = (millis() - startTime)/1000;
  // sendToDatabase(temperature_c, humidity, pressure, dewpoint_c, illumination, battery_voltage, solar_panel_voltage);
  sendToInfluxDB(temperature_c, humidity, pressure, dewpoint_c, illumination, battery_voltage, solar_panel_voltage);

  if(temperature_c != -1000 || humidity != -1000 || pressure != -1000){
    sendToWeatherUnderground(temperature_f, humidity, baromin, dewpoint_f);
  }
  else {
    serialLog("Can not send data to WeatherUnderground");
  }

  sendLog();

  isolate_all_rtc_gpio();
  WiFi.mode(WIFI_OFF);
  unsigned long sleepTime = (CYCLE_TIME_SEC - activeTime) * 1000000;
  serialLog("Entering deep sleep for " + String(sleepTime/1000000) +" seconds...");
  esp_sleep_enable_timer_wakeup(sleepTime);
  esp_deep_sleep_start();
}

void loop() {
  // Empty, everything is done in setup()
}
