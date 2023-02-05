#include <AirGradient.h>
// DNSServer.h is included to help platformio's dependency manager without
// the need to resort to "deep" mode.
#include <DNSServer.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <U8g2lib.h>
#include <WiFiClient.h>
/*
BUILD PARAMETER DEPENDENT SETUP
*/
#if defined(PUSH_ENABLED) || defined(SERVER_ENABLED)
    #ifndef WIFI_ENABLED
        #define WIFI_ENABLED
    #endif
#endif

#ifdef PUSH_ENABLED
    #ifndef PUSH_INSTANCE
        #error "PUSH_INSTANCE is required when PUSH_ENABLED"
    #endif
    #ifndef PUSH_USER
        #error "PUSH_USER is required when PUSH_ENABLED"
    #endif
    #ifndef PUSH_PASSWORD
        #error "PUSH_PASSWORD is required when PUSH_ENABLED"
    #endif
const auto pushFrequency = 60000U;
uint32_t lastPush;
#endif

#ifdef WIFI_ENABLED
    #ifndef WIFI_SSID
        #error "WIFI_SSID is required when WIFI_ENABLED"
    #endif
    #ifndef WIFI_PASSWORD
        #error "WIFI_PASSWORD is required when WIFI_ENABLED"
    #endif
#endif

#ifndef DEVICE_NAME
    #define DEVICE_NAME "ag_basic"
#endif

#ifdef SERVER_ENABLED
    #ifndef SERVER_PORT
        #define SERVER_PORT 80
    #endif
auto server = ESP8266WebServer(SERVER_PORT);
String labels;
#endif
/*
/BUILD PARAMETER DEPENDENT SETUP
*/

const auto fetchFrequencyMS = 30000U;
uint32_t lastFetch;
// captured values
String temp;
String rh;
String co2;
String pm1;
String pm2;
String pm10;

auto sensors = AirGradient();
auto display = U8G2_SSD1306_64X48_ER_1_HW_I2C(/* rotation=*/U8G2_R0, /* reset=*/U8X8_PIN_NONE);

void displayThreeLines(const char* l1, const char* l2, const char* l3) {
    display.firstPage();
    do {
        display.setFont(u8g2_font_t0_16_tf);
        display.drawStr(1, 10, l1);
        display.drawStr(1, 28, l2);
        display.drawStr(1, 46, l3);
    } while (display.nextPage());
}

void fetchSensorsAndDisplay(uint32_t now) {
    if (((now - lastFetch) <= fetchFrequencyMS) && lastFetch != 0) {
        return;
    }

    lastFetch = now;

    TMP_RH tmpRh = sensors.periodicFetchData();
    if (tmpRh.t > 999.9) {
        tmpRh.t = 999.9;
    }
    if (tmpRh.t < -99.9) {
        tmpRh.t = -99.9;
    }

    if (tmpRh.rh > 100) {
        tmpRh.rh = 100;
    }
    if (tmpRh.rh < 0) {
        tmpRh.rh = 0;
    }
    temp = String(tmpRh.t);
    rh = String(tmpRh.rh);
    co2 = String(sensors.getCO2_Raw());

    /*
    TODO:
      Modify lib to get all particulate data in one shot.
      Currently it will reach out to the sensor for each
      value, although all values are contained in the
      singular sensors respone.
    */
    pm1 = String(sensors.getPM1_Raw());
    pm2 = String(sensors.getPM2_Raw());
    pm10 = String(sensors.getPM10_Raw());

    displayThreeLines(("C " + temp).c_str(),
                      ("RH " + rh).c_str(),
                      ("CO2 " + co2).c_str());
}

#ifdef PUSH_ENABLED
void pushInfluxMetrics(uint32_t now) {
    if ((now - lastPush) <= pushFrequency) {
        return;
    }
    lastPush = now;

    WiFiClientSecure connection;
    /*
      uh ohhh :D
      We should(TM) set the accepted fingerprint here, like in
      setFingerprint(uint8_t[]). But then we would have to renew it when the
      certificate changes because we can't afford something like a trust store
      with our teeny tiny memory budget. Also, keeping track of the fingerprint
      sounds very stressful - so lets join the ~internet of shit~ :/

      Maybe, somewhere in the future, insecure will be the fallback if we cannot
      find a matching fingerprint. We are already pushing stuff, we have a
      display and a (eye searing bright) LED on board, so we could go into ALERT
      mode if this is the case. The boss then has to update the firmware - maybe
      not so bad and probably better that doing the
      possibly-privacy-data-leaking thing we do now. (you know: "Hoody-Hackerboy
      found out that the CO2 is very constant, maybe no one is at home and one
      could take some stuff that doesn't belong to them without being
      disturbed..."
      )
    */
    connection.setInsecure();

    HTTPClient http;
    http.begin(connection, "https://influx-" PUSH_INSTANCE ".grafana.net/api/v1/push/influx/write");
    http.addHeader("Authorization", "Bearer " PUSH_USER ":" PUSH_PASSWORD);
    http.addHeader("content-type", "application/json");

    char buf[256];
    auto len = snprintf(buf, 256,
                        "airquality,sender_id=%s temp=%s,rh=%s,co2=%s,pm1=%s,pm2=%s,pm10=%s",
                        DEVICE_NAME, temp.c_str(), rh.c_str(), co2.c_str(), pm1.c_str(), pm2.c_str(), pm10.c_str());
    if (len > 256) {
        Serial.println("push metric was truncated...");
    }

    auto responseCode = http.POST(buf);
    http.end();

    if (responseCode >= 400) {
        Serial.println("HTTP error " + String(responseCode));
    }
}
#endif

#ifdef WIFI_ENABLED
void setupWifi() {
    WiFi.mode(WIFI_STA); // enforce station mode without access point
    wifi_station_set_hostname(DEVICE_NAME);
    WiFi.setHostname(DEVICE_NAME);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int attempts = 1;
    while (WiFi.status() != WL_CONNECTED) {
        displayThreeLines("connecting",
                          WIFI_SSID,
                          (attempts & 1) == 1 ? "please" : "wait");
        delay(500);
        attempts++;
    }
}
#endif

#ifdef SERVER_ENABLED
void setupLabels() {
    char extra[64];
    #ifdef LOCATION
    snprintf(extra, 64, ",location=\"%s\"", LOCATION);
    #endif

    char buf[128];
    snprintf(buf, 128,
             "collection=\"Airgradient\",device=\"%s\"%s",
             DEVICE_NAME, extra);

    labels = String(buf);
}

void appendPromGauge(String& current, const char* name, const char* help, const char* value) {
    char buf[256];
    int len = snprintf(buf, 256,
                       "#HELP %s %s\n"
                       "#TYPE %s gauge\n"
                       "%s{%s} %s\n",
                       name, help, name, name, labels.c_str(), value);
    if (len > 256) {
        Serial.printf("prom metric (%s) was truncated...\n", name);
    }
    current += buf;
}

auto promMetrics = String();

void serveMetrics() {
    promMetrics.clear();
    appendPromGauge(promMetrics, "temperature", "Temperature in °C", temp.c_str());
    appendPromGauge(promMetrics, "humidity", "Realative humidity in %", rh.c_str());
    appendPromGauge(promMetrics, "CO2", "CO2 concentraion in PPM", co2.c_str());
    appendPromGauge(promMetrics, "particulate_matter_1", "particle count (1µm and below, atmospheric environment) in µg/m³", pm1.c_str());
    appendPromGauge(promMetrics, "particulate_matter_2_5", "particle count (1µm to 2.5µm and below, atmospheric environment) in µg/m³", pm2.c_str());
    appendPromGauge(promMetrics, "particulate_matter_10", "particle count (2.5µm to 10µm, atmospheric environment) in µg/m³", pm10.c_str());

    server.send(200, "text/plain;charset=utf-8", promMetrics);
}

void setupServer() {
    setupLabels();

    server.on("/", serveMetrics);
    server.on("/metrics", serveMetrics);
    server.onNotFound(std::function<void(void)>(
        []() { server.send(404, "text/plain", "not found"); }));
    server.onFileUpload(std::function<void(void)>(
        []() { server.send(405, "text/plain", "not allowed"); }));

    server.begin();

    displayThreeLines("serving metrics", "on port", String(SERVER_PORT).c_str());
    delay(1000);
}
#endif

void setup() {
    Serial.begin(9600);

    display.begin();

    displayThreeLines("CO2 init", "", "");
    sensors.CO2_Init();
    displayThreeLines("CO2 done", "PMS init", "");
    sensors.PMS_Init();
    displayThreeLines("CO2 done", "PMS done", "TRH init");
    sensors.TMP_RH_Init(0x44);
    displayThreeLines("CO2 done", "PMS done", "TRH done");

#ifdef WIFI_ENABLED
    setupWifi();
#endif

#ifdef SERVER_ENABLED
    setupServer();
#endif

    fetchSensorsAndDisplay(millis());
}

void loop() {
    auto t = millis();
    fetchSensorsAndDisplay(t);
#ifdef SERVER_ENABLED
    server.handleClient();
#endif
#ifdef PUSH_ENABLED
    pushInfluxMetrics(t);
#endif
}
