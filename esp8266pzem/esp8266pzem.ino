// VERSION 2 with WiFIManager integration for device_name
#include <FS.h>                   //this needs to be first, or it all crashes and burns...

#include <ESP8266WiFi.h>          // ESP8266 Core WiFi Library (you most likely already have this in your sketch)
#include <Ticker.h>               // for LED status

#include <DNSServer.h>            // Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     // Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          // https://github.com/tzapu/WiFiManager WiFi Configuration Magic

#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson

#include <ESP8266HTTPClient.h>
#include <SoftwareSerial.h>
#include <PZEM004T.h>             // https://github.com/olehs/PZEM004T Power Meter


// select which pin will trigger the configuration portal when set to LOW
#define TRIGGER_PIN 0
#define MAX_ATTEMPTS 10

PZEM004T pzem(4,5);  // RX,TX (D2, D1) on NodeMCU
IPAddress ip(192,168,1,1); // required by pzem but not used

HTTPClient http;
Ticker ticker;
boolean led = false;
char device_name[40];
WiFiManagerParameter custom_device_name("name", "device name", device_name, 40, " required");

void tick() {
  //toggle state
  led = !led;
  pinMode(BUILTIN_LED, OUTPUT);
  digitalWrite(BUILTIN_LED, led);     // set pin to the opposite state
}

//gets called when WiFiManager enters configuration mode
void configModeCallback (WiFiManager *myWiFiManager) {
  //entered config mode, make led toggle faster
  ticker.attach(0.2, tick);
}

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("saving config");
  strcpy(device_name, custom_device_name.getValue());
  DynamicJsonBuffer jsonBuffer;
  JsonObject& json = jsonBuffer.createObject();
  json["device_name"] = device_name;
  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) {
     Serial.println("failed to open config file for writing");
  }
  json.printTo(Serial);
  json.printTo(configFile);
  configFile.close();
}

boolean readConfig() {
  // clean FS, for testing
  // SPIFFS.format();

  //read configuration from FS json
  Serial.println("mounting FS...");
  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");
          strcpy(device_name, json["device_name"]);
          return true;
        } else {
          Serial.println("failed to load json config");
          return false;
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
    return false;
  }
  //end read
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println("Starting setup");
  // set led pin as output
  pinMode(BUILTIN_LED, OUTPUT);

  // start ticker with 0.6 because we start in AP mode and try to connect
  ticker.attach(0.6, tick);

  // WiFiManager
  // Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;
  // wifiManager.setDebugOutput(false);

  // reset settings - for testing
  // wifiManager.resetSettings();

  // sets timeout until configuration portal gets turned off
  // useful to make it all retry or go to sleep in seconds
  wifiManager.setTimeout(180);

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  // set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
  wifiManager.setAPCallback(configModeCallback);

  //add all your parameters here
  wifiManager.addParameter(&custom_device_name);

  // wifiManager.setCustomHeadElement("<style>html{filter: invert(100%); -webkit-filter: invert(100%);}</style>");

  if(!readConfig()) {
    wifiManager.resetSettings();
  }

  // fetches ssid and pass and tries to connect
  // if it does not connect it starts an access point with auto generated name from 'ESP' and the esp's Chip ID use
  // and goes into a blocking loop awaiting configuration
  String ssid = "POWER_MONITOR_" + String(ESP.getChipId());
  if(!wifiManager.autoConnect(ssid.c_str(), NULL)) {
    Serial.println("failed to connect and hit timeout");
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }

  // if you get here you have connected to the WiFi
  ticker.detach();
  // keep LED on
  digitalWrite(BUILTIN_LED, LOW);

  pzem.setAddress(ip);
  delay(5000);
  // keep LED off
  digitalWrite(BUILTIN_LED, HIGH);
}

// could make generic and pass a pointer to the right function...
float getVoltage() {
  int i = 0;
  float r = -1.0;
  do {
    r = pzem.voltage(ip);
    wdt_reset();
    i++;
  } while ( i < MAX_ATTEMPTS && r < 0.0);
  return r;
}

float getCurrent() {
  int i = 0;
  float r = -1.0;
  do {
    r = pzem.current(ip);
    wdt_reset();
    i++;
  } while ( i < MAX_ATTEMPTS && r < 0.0);
  return r;
}

float getPower() {
  int i = 0;
  float r = -1.0;
  do {
    r = pzem.power(ip);
    wdt_reset();
    i++;
  } while ( i < MAX_ATTEMPTS && r < 0.0);
  return r;
}

float getEnergy() {
  int i = 0;
  float r = -1.0;
  do {
    r = pzem.energy(ip);
    wdt_reset();
    i++;
  } while ( i < MAX_ATTEMPTS && r < 0.0);
  return r;
}

void sendMeasures() {
  float v = getVoltage();
  Serial.print(v);Serial.print("V; ");

  float i = getCurrent();
  Serial.print(i);Serial.print("A; ");

  float p = getPower();
  Serial.print(p);Serial.print("W; ");

  float e = getEnergy();
  Serial.print(e);Serial.print("Wh; ");

  String payload = "";
  if(v >= 0.0) payload += "voltage,device_name=" + String(device_name) + " value=" + String(v, 2) + "\n";
  if(i >= 0.0) payload += "current,device_name=" + String(device_name) + " value=" + String(i, 2) + "\n";
  if(p >= 0.0) payload += "power,device_name=" + String(device_name) + " value=" + String(p, 2) + "\n";
  if(e >= 0.0) payload += "energy,device_name=" + String(device_name) + " value=" + String(e, 2) + "\n";
  Serial.print(payload);
  if (payload=="") {
    Serial.println();
    return;
  }
  http.begin("[HOST]/influxdb/write?db=power");
  http.addHeader("cache-control", "no-cache");
  http.setAuthorization("[HASH]");

  int httpCode  = http.POST(payload);
  Serial.print(httpCode);
  if (httpCode < 0) {
    Serial.println();
    Serial.printf("[HTTP]... failed, error: %s\n", http.errorToString(httpCode).c_str());
    Serial.println();
    Serial.print(payload);
  }
  http.end();

  Serial.println();
}

void loop() {
  // put your main code here, to run repeatedly:
  sendMeasures();
  delay(5000);

  // is configuration portal requested?
  if ( digitalRead(TRIGGER_PIN) == LOW ) {
    digitalWrite(BUILTIN_LED, LOW);
    WiFiManager wifiManager;
    wifiManager.resetSettings();
    SPIFFS.format();
    ESP.reset();
    delay(5000);
  }
}