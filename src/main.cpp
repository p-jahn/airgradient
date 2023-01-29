#include <AirGradient.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <U8g2lib.h>
#include <WiFiClient.h>

/*
BUILD PARAMETER DEPENDENT SETUP
*/
#if defined(PUSH_ENABLED) || defined(SERVER_ENABLED)
    #define WIFI_ENABLED
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
const int pushFrequency = 60000;
long lastPush;
#endif

#ifdef WIFI_ENABLED
    #ifndef WIFI_PASSWORD
        #error "WIFI_PASSWORD is required when WIFI_ENABLED"
    #endif
    #ifndef WIFI_PASSWORD
        #error "WIFI_PASSWORD is required when WIFI_ENABLED"
    #endif
    #ifndef WIFI_HOST_NAME
        #define WIFI_HOST_NAME "ag_basic"
    #endif
const auto deviceID = String(ESP.getChipId(), HEX).c_str();
#endif

#ifdef SERVER_ENABLED
    #ifndef SERVER_PORT
        #define SERVER_PORT 80
    #endif
auto server = ESP8266WebServer(SERVER_PORT);
#endif
/*
/BUILD PARAMETER DEPENDENT SETUP
*/

const int fetchFrequencyMS = 30000;
long lastFetch;
// captured values
char temp[6]; // "-99.9" to "999.9", + "\0"
char rh[4];   // "0" to "100", + "\0"
char co2[5];  // "0" to "9999", + "\0"
char pm1[6];  // "0" to "65536", + "\0"
char pm2[6];  // "0" to "65536", + "\0"
char pm10[6]; // "0" to "65536", + "\0"

auto sensors = AirGradient();
auto display = U8G2_SSD1306_64X48_ER_1_HW_I2C(/* rotation=*/U8G2_R0, /* reset=*/U8X8_PIN_NONE);

void setup() {
    Serial.begin(115200);

    display.begin();

#ifdef WIFI_ENABLED
    setupWifi();
#endif
#ifdef SERVER_ENABLED
    setupServer();
#endif
}

void loop() {
    auto t = millis();
    fetchSensorsAndDisplay(t);
#ifdef PUSH_ENABLED
    pushInfluxMetrics(t);
#endif
}

void displayThreeLines(const char* l1, const char* l2, const char* l3) {
    display.firstPage();
    do {
        display.setFont(u8g2_font_t0_16_tf);
        display.drawStr(1, 10, l1);
        display.drawStr(1, 28, l2);
        display.drawStr(1, 46, l3);
    } while (display.nextPage());
}

void fetchSensorsAndDisplay(long now) {
    /*
    TOOD: handle overflows
      We have 32 bits of milliseconds to work with
      which is less than 25 days until overflow.
      ... something like [last = curr < last ? long.MaxValue - last + curr : curr]
    */

    if ((now - lastFetch) <= fetchFrequencyMS) {
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

    /*
    TODO:
      Modify lib to get all particulate data in one shot.
      Currently it will reach out to the sensor for each
      value, although all values are contained in the
      singular sensors respone.
    */
    itoa(sensors.getPM1_Raw(), pm1, 10);
    itoa(sensors.getPM2_Raw(), pm2, 10);
    itoa(sensors.getPM10_Raw(), pm10, 10);

    displayThreeLines(strcat("°C ", dtostrf(tmpRh.t, /* left-align x.x */ -3, 1, temp)),
                      strcat("RH% ", itoa(tmpRh.rh, rh, 10)),
                      strcat("CO2 ", itoa(sensors.getCO2_Raw(), co2, 10)));
}

#ifdef PUSH_ENABLED
void pushInfluxMetrics(long now) {
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
      sounds very stressful - so lets joint the ~internet of shit~ :/

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

    String header = "airquality,sender_id=" + String(deviceID);
    String metrics = "temp=" + String(temp) + ",rh=" + String(rh) +
                     ",co2=" + String(co2) + ",pm1=" + String(pm1) +
                     ",pm2=" + String(pm2) + ",pm10=" + String(pm10);
    String payload = header + " " + metrics;
    auto responseCode = http.POST(payload);
    http.end();

    if (responseCode >= 400) {
        Serial.println("HTTP error " + String(responseCode));
    }
}
#endif

#ifdef WIFI_ENABLED
void setupWifi() {
    WiFi.mode(WIFI_STA); // enforce station mode without access point
    WiFi.setHostname(WIFI_HOST_NAME);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int attempts = 1;
    while (WiFi.status() != WL_CONNECTED) {
        displayThreeLines("connecting to",
                          WIFI_SSID,
                          attempts & 1 == 1 ? "just a moment" : "please wait");
        delay(500);
        attempts++;
    }
}
#endif

#ifdef SERVER_ENABLED
void setupServer() {
    server.on("/", serveMetrics);
    server.onNotFound(std::function<void(void)>(
        []() { server.send(404, "text/plain", "not found"); }));
    server.onFileUpload(std::function<void(void)>(
        []() { server.send(405, "text/plain", "not allowed"); }));

    displayThreeLines("serving metrics", "on port", String(SERVER_PORT).c_str());
}

// TODO: move to prometheus format
void serveMetrics() {
    /*     String idString = "{\"id\": \"" + deviceID + "\", \"mac\": \"" +
                          WiFi.macAddress().c_str() + "\"}";

        String json = "{\n";
        json += "  \"device\": " + idString + ",\n";
        json += "  \"tempInC\": " + String(temp) + ",\n";
        json += "  \"RH\": " + String(rh) + ",\n";
        json += "  \"CO2\": " + String(co2) + ",\n";
        json += "  \"PM1\": " + String(pm1) + ",\n";
        json += "  \"PM2\": " + String(pm2) + ",\n";
        json += "  \"PM10\": " + String(pm10) + ",\n";
        json += "}";

        server.send(200, "application/json", json); */
}

char* promGauge(char* name) {
    return "todo";
}
#endif
