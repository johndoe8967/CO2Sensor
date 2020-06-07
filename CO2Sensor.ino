
/*
  SimpleMQTTClient.ino
  The purpose of this exemple is to illustrate a simple handling of MQTT and Wifi connection.
  Once it connects successfully to a Wifi network and a MQTT broker, it subscribe to a topic and send a message to it.
  It will also send a message delayed 5 seconds later.
*/
#include "EspMQTTClient.h"
#include <NTPClient.h>
#include "MHZ19.h"
#include <ArduinoJson.h>
#include "infrastructure.h"


// Variables for MH Z19B Sensor
#define RX_PIN 16                                          // Rx pin which the MHZ19 Tx pin is attached to
#define TX_PIN 17                                          // Tx pin which the MHZ19 Rx pin is attached to
#define BAUDRATE 9600                                      // Device to MH-Z19 Serial baudrate (should not be changed)
HardwareSerial mySerial(2);                              // (ESP32 Example) create device to MH-Z19 serial
MHZ19 myMHZ19;                                             // Constructor for library
int CO2;
int CO2uncorrected;
int Temp;


// MQTT client settings
#include "CredetialSettings.h"
EspMQTTClient MQTTClient(
  MQTTHostname,     // MQTT Broker server ip
  MQTTPort,         // The MQTT port, default to 1883. this line can be omitted
  MQTTUser,         // Can be omitted if not needed
  MQTTPassword,     // Can be omitted if not needed
  MQTTClientName    // Client name that uniquely identify your device
);

// Network Time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

unsigned long getDataTimer = 0;
unsigned long updateIntervall = 10000;
String message;
bool timeValid = false;
bool lastTimeValid = false;

void setup()
{
  Serial.begin(115200);
  Serial.println("Booting");

  wifiInit();       // get WiFi connected
  IP_info();

  setupOTA();
  String hostname = MQTTClientName;
  hostname += ".local";
  setupRemoteDebug(hostname.c_str());

  MQTTClient.enableDebuggingMessages(); // Enable debugging messages sent to serial output
  MQTTClient.enableLastWillMessage("device/lastwill", MQTTClientName);

  // Setup MH-Z19 CO2 Sensor over serial interface
  mySerial.begin(BAUDRATE, SERIAL_8N1, RX_PIN, TX_PIN); // (ESP32 Example) device to MH-Z19 serial start
  myMHZ19.begin(mySerial);                                // *Serial(Stream) refence must be passed to library begin().
  myMHZ19.autoCalibration();                              // Turn auto calibration ON (OFF autoCalibration(false))
}

// This function is called once everything is connected (Wifi and MQTT)
void onConnectionEstablished()
{
  MQTTClient.subscribe("CO2Sensor/#", [](const String & topic, const String & payload) {
    debugD("Received message: %s,%s", topic.c_str(), payload.c_str());
    if (topic == "CO2Sensor/update") {

      const int capacity = JSON_OBJECT_SIZE(3) + 2 * JSON_OBJECT_SIZE(1);
      StaticJsonDocument<capacity> doc;
      DeserializationError err = deserializeJson(doc, payload);
      if (err) {
        debugD("deserializeJson() failed with code %s", err.c_str());
      }

      if (doc.containsKey("Intervall")) {
        auto receivedVal = doc["Intervall"].as<unsigned long>();
        debugD("Received Intervall: %lu", receivedVal);
        if ((receivedVal > 1000) && (receivedVal < 3600000)) {
          updateIntervall = receivedVal;
        }
      }
      if (doc.containsKey("Debug")) {
        MQTTClient.enableDebuggingMessages(doc["Debug"].as<bool>());
        debugD("Received MQTTDebug over Serial");
      }
    }
    if (topic == "CO2Sensor/commands") {
      if (payload == "getCommands") {
        debugD("Received get commands");
        String commands = "{\"commands\":[{\"cmd\":\"Intervall\",\"type\":\"float\",\"min\":1000,\"max\":3600000},{\"cmd\":\"Debug\",\"type\":\"bool\"}]}";
        MQTTClient.publish("CO2Sensor/commands", commands);
      }
    }

  });
  MQTTClient.subscribe("device/#", [](const String & topic, const String & payload) {
    if (topic == "device/scan") {
      if (payload == "scan") {
        debugD("Received device scan");
        MQTTClient.publish("device/scan", MQTTClientName);
      }
    }
  });

  timeClient.begin();
}

void loop()
{
  ArduinoOTA.handle();
  Debug.handle();
  MQTTClient.loop();

  timeValid = timeClient.update();
  if (!lastTimeValid && timeValid) {
    timeValid = true;
    debugA("Got new Time: %s", timeClient.getFormattedTime());
  }
  lastTimeValid = timeValid;

  if (millis() - getDataTimer >= updateIntervall) {
    getDataTimer = millis();

    CO2 = myMHZ19.getCO2(true);                             // Request CO2 (as ppm)
    CO2uncorrected = myMHZ19.getCO2(false);
    Temp = myMHZ19.getTemperature();                     // Request Temperature (as Celsius)

    if (MQTTClient.isConnected()) {
      String strTime = "";
      unsigned int ms = 0;
      strTime += timeClient.getEpochTime(ms);
      String milliseconds = "000";
      milliseconds += ms;
      milliseconds = milliseconds.substring(milliseconds.length() - 3);
      strTime += milliseconds;

      // Publish a message to "mytopic/test"
      message = "[{\"name\":\"";
      message += MQTTClientName;
      message += "\",\"field\":\"CO2\",\"value\":";
      message += CO2;
      message += ",\"Temp\":";
      message += Temp;
      message += ",\"time\":";
      message += strTime;
      message += "},";

      message += "{\"name\":\"";
      message += MQTTClientName;
      message += "\",\"field\":\"CO2uncorrected\",\"value\":";
      message += CO2uncorrected;
      message += ",\"time\":";
      message += strTime;
      message += "}]";
      debugD("MQTT Publish: %s", message.c_str());
      MQTTClient.publish("sensors", message); // You can activate the retain flag by setting the third parameter to true
    } else {
      debugD("MQTT not connected");
    }
  }
}
