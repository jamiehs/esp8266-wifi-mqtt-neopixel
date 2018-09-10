// Flash with 4M (1M SPIFFS)

#include <FS.h>
#include <PubSubClient.h>        // https://github.com/knolleary/pubsubclient
#include <ESP8266WiFi.h>         // https://github.com/esp8266/Arduino
#include <DNSServer.h>           // https://github.com/esp8266/Arduino/tree/master/libraries/DNSServer
#include <WiFiManager.h>         // https://github.com/tzapu/WiFiManager
#include <ArduinoJson.h>         // https://github.com/bblanchon/ArduinoJson
#include <FastLED.h>

#define MOTION_PIN 14
#define DHT11_PIN 12

#define LED_PIN     2
#define COLOR_ORDER GRB
#define CHIPSET     WS2812B
#define NUM_LEDS    60 // These take roughly 2 amps at full white for 60
#define UPDATES_PER_SECOND 120
#define MIN_BRIGHTNESS 38

#define MQTT_STATE_ON_PAYLOAD   "ON"
#define MQTT_STATE_OFF_PAYLOAD  "OFF"


CRGB leds[NUM_LEDS];

uint8_t r = 0;
uint8_t g = 0;
uint8_t b = 0;

String on_off_state = "ON";
long brightness = 127;
long prevBrightness = 127;

char device_slug[40];
char mqtt_server[40];
char friendly_name[40];
char mqtt_port[6] = "1883";
char mqtt_user[40];
char mqtt_password[40];
String mac = WiFi.macAddress();
String topic = "homeassistant/light";
boolean has_motion = false;

//flag for saving data
bool shouldSaveConfig = false;


StaticJsonBuffer<256> staticJsonBuffer;
char jsonBuffer[256] = {0};


//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

WiFiClient espClient;
PubSubClient mqttClient(espClient);
WiFiManager wifiManager;


// Handler for the incoming MQTT topics and payloads
void mqttCallback(char* incomingTopic, byte* payload, unsigned int length) {
  payload[length] = '\0';
  String strTopic = String((char*)incomingTopic);
  String msg = String((char*)payload);

  if (String(topic + "/set").equals(incomingTopic)) {
    DynamicJsonBuffer dynamicJsonBuffer;
    JsonObject& root = dynamicJsonBuffer.parseObject(payload);
    if (!root.success()) {
      Serial.println("ERROR: parseObject() failed");
      return;
    }

    if (root.containsKey("state")) {
      Serial.println("changing state");
      String prev_on_off_state = on_off_state;
      Serial.println(root["state"].asString());
      on_off_state = root["state"].asString();
      if (strcmp(root["state"], MQTT_STATE_ON_PAYLOAD) == 0) {
        if(prev_on_off_state == "OFF") {
          fadeBrightness(0, prevBrightness);
        }
      } else if (strcmp(root["state"], MQTT_STATE_OFF_PAYLOAD) == 0) {
        if(prev_on_off_state == "ON") {
          prevBrightness = brightness;
          fadeBrightness(brightness, 0);
        }
      }
    }

    if (root.containsKey("color")) {
      Serial.println("changing color");

      uint8_t newR = root["color"]["r"];
      uint8_t newG = root["color"]["g"];
      uint8_t newB = root["color"]["b"];

      fadeBetween(r, g, b, newR, newG, newB);

    }

    if (root.containsKey("brightness")) {
      Serial.println("setting brightness");
      uint8_t newBrightness = root["brightness"];
      if (newBrightness == 0) {
        prevBrightness = brightness;
      }
      fadeBrightness(brightness, newBrightness);
    }
  }



  sendState();


  Serial.println("----------------------");
  Serial.println(strTopic);
  Serial.println(msg);
  Serial.println("----------------------");

}


void sendState() {
  DynamicJsonBuffer dynamicJsonBuffer;
  JsonObject& root = dynamicJsonBuffer.createObject();
  root["state"] = on_off_state;
  root["brightness"] = brightness;
  JsonObject& color = root.createNestedObject("color");
  color["r"] = r;
  color["g"] = g;
  color["b"] = b;
  root.printTo(jsonBuffer, sizeof(jsonBuffer));

  Serial.println(jsonBuffer);

  mqttClient.publish(String(topic + "/state").c_str(), String(jsonBuffer).c_str(), true);
}

void reconnect() {
  // Loop until we're reconnected
  while(!mqttClient.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    // If you do not want to use a username and password, change next line to
    // if (mqttClient.connect("ESP8266Client")) {
    if (mqttClient.connect(mac.c_str(), mqtt_user, mqtt_password)) {
      Serial.println("connected");
      // Subscribe to our globally defined topic
      mqttClient.subscribe(String(topic + "/set").c_str());
      // Say Hello
      mqttClient.publish(String(topic + "/status").c_str(), String("BOOTED").c_str(), true);


      JsonObject& root = staticJsonBuffer.createObject();
      root["name"] = friendly_name;
      root["platform"] = "mqtt_json";
      root["state_topic"] = topic + "/state";
      root["command_topic"] = topic + "/set";
      root["brightness"] = true;
      root["rgb"] = true;
      root.printTo(jsonBuffer, sizeof(jsonBuffer));
      Serial.println(jsonBuffer);
      mqttClient.publish(String(topic + "/config").c_str(), String(jsonBuffer).c_str(), true);
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(9600);
  Serial.println();

  //clean FS, for testing
  // SPIFFS.format();

  //read configuration from FS json
  Serial.println("mounting FS...");
  Serial.print("MAC Address: ");
  Serial.println(mac);

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

          strcpy(device_slug, json["device_slug"]);
          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(friendly_name, json["friendly_name"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          strcpy(mqtt_user, json["mqtt_user"]);
          strcpy(mqtt_password, json["mqtt_password"]);

        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read


  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter custom_device_slug("device_slug", "Device Slug", device_slug, 40);
  WiFiManagerParameter custom_friendly_name("name", "Name", friendly_name, 40);
  WiFiManagerParameter custom_mqtt_server("server", "MQTT Server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "MQTT Port", mqtt_port, 6);
  WiFiManagerParameter custom_mqtt_user("user", "MQTT User", mqtt_user, 40);
  WiFiManagerParameter custom_mqtt_password("password", "MQTT Password", mqtt_password, 40);

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //add all your parameters here
  wifiManager.addParameter(&custom_device_slug);
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_friendly_name);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_user);
  wifiManager.addParameter(&custom_mqtt_password);

  //reset settings - for testing
  // wifiManager.resetSettings();

  if (!wifiManager.autoConnect()) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }

  Serial.println("connected...yeey :)");

  //read updated parameters
  strcpy(device_slug, custom_device_slug.getValue());
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(friendly_name, custom_friendly_name.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(mqtt_user, custom_mqtt_user.getValue());
  strcpy(mqtt_password, custom_mqtt_password.getValue());
  topic = topic + "/" + String(device_slug);

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["device_slug"] = device_slug;
    json["mqtt_server"] = mqtt_server;
    json["friendly_name"] = friendly_name;
    json["mqtt_port"] = mqtt_port;
    json["mqtt_user"] = mqtt_user;
    json["mqtt_password"] = mqtt_password;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }

  Serial.println("local ip");
  Serial.println(WiFi.localIP());

  // MQTT Setup & Callback
  mqttClient.setServer(mqtt_server, atol(mqtt_port));
  mqttClient.setCallback(mqttCallback);

  FastLED.addLeds<CHIPSET, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection( TypicalLEDStrip );
  FastLED.setBrightness( 127 );

  fadeBetween(0, 0, 0, 255, 36, 255);
}

void loop() {
  unsigned long currentMillis = millis();

  // Flash button being held
  if(digitalRead(0) == 0) {
    Serial.println("Attempting to clear flash...");
    delay(1000);

    Serial.println("clearing SPIFFS...");
    SPIFFS.format();
    delay(3000);

    Serial.println("resetting WiFi settings...");
    wifiManager.resetSettings();
    delay(3000);

    Serial.println("rebooting...");
    ESP.reset();
    delay(5000);
  }

  if (!mqttClient.connected()) {
    reconnect();
  }
  mqttClient.loop();

  FastLED.show();
}

void fadeBetween(int oldR, int oldG, int oldB, int newR, int newG, int newB) {
  // Guard against division by zero
  int numSteps = 500;

  r = newR;
  g = newG;
  b = newB;
  sendState();

  // Calculate how how much each colour needs to change on each step
  const float
    stepR = (newR - oldR) / (float)numSteps,
    stepG = (newG - oldG) / (float)numSteps,
    stepB = (newB - oldB) / (float)numSteps;

  // These values will store our colours on the way along
  float localR = oldR, localG = oldG, localB = oldB;
  int byteR = oldR, byteG = oldG, byteB = oldB;

  // Go through each fade step
  for (int step = 0; step < numSteps; ++step) {
    // Move one step towards the target colour
    localR += stepR;
    localG += stepG;
    localB += stepB;

    // Round the colours to integers here so we don't have to do it repeatedly in the loop below
    byteR = (int)(localR + 0.5f);
    byteG = (int)(localG + 0.5f);
    byteB = (int)(localB + 0.5f);

    for(int i = 0; i < NUM_LEDS; i++) {
      CRGB newColor = CRGB(byteR, byteG, byteB);
      leds[i] = newColor;
    }

    FastLED.show();
  }
}

void fadeBrightness(int oldBrightness, int newBrightness) {
  brightness = newBrightness;
  sendState();

  // Guard against division by zero
  int numSteps = 300;

  // Calculate how how much each colour needs to change on each step
  const float
    stepBrightness = (newBrightness - oldBrightness) / (float)numSteps;

  // These values will store our colours on the way along
  float localBrightness = oldBrightness;
  int byteBrightness = oldBrightness;

  // Go through each fade step
  for (int step = 0; step < numSteps; ++step) {
    // Move one step towards the target colour
    localBrightness += stepBrightness;

    // Round the colours to integers here so we don't have to do it repeatedly in the loop below
    byteBrightness = (int)(localBrightness + 0.5f);

    FastLED.setBrightness( byteBrightness );

    FastLED.show();
  }
}

