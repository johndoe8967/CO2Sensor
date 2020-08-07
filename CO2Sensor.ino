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

#define debug true
TaskHandle_t Task1;

typedef struct Measurements {
  float CO2;
  char CO2Error;
  float CO2unlimited;
  char CO2unlimitedError;
  float CO2Raw;
  char CO2RawError;
  float Temp;
  char TempError;
  float Accuracy;
  char AccuracyError;
  unsigned long time;
  unsigned int ms;
} Measurement;

unsigned int measureIndex = 0;
unsigned int sendMeasureIndex = 0;
#define MeasureIndexBits 5
#define maxMeasurements (1<<MeasureIndexBits)
Measurement measures[maxMeasurements];
unsigned int countStoredMeasurements() {
  if (measureIndex == sendMeasureIndex) {
    sendMeasureIndex = (measureIndex - 1) & ((1 << MeasureIndexBits) - 1);
  }
  if (measureIndex < sendMeasureIndex) {
    return measureIndex +  maxMeasurements - sendMeasureIndex - 1;
  } else {
    return measureIndex - sendMeasureIndex - 1;
  }
}
void incMeasureIndex() {
  measureIndex++;
  measureIndex &= ((1 << MeasureIndexBits) - 1);
}
void incSendMeasureIndex() {
  sendMeasureIndex++;
  sendMeasureIndex &= ((1 << MeasureIndexBits) - 1);
}

enum sendStates {stopped, tostart, startdelay, started, tostop};
enum sendStates sendState;
unsigned long stoptime;
bool disableSleep = false;

#define SWVersion "CO2Sensor V1.2"

// Variables for MH Z19B Sensor
#define RX_PIN 16                                          // Rx pin which the MHZ19 Tx pin is attached to
#define TX_PIN 17                                          // Tx pin which the MHZ19 Rx pin is attached to
#define BAUDRATE 9600                                      // Device to MH-Z19 Serial baudrate (should not be changed)
HardwareSerial mySerial(2);                              // (ESP32 Example) create device to MH-Z19 serial
MHZ19 myMHZ19;                                             // Constructor for library
int CO2;
int CO2unlimited;
float CO2Raw;
float Temp;
int Accuracy;

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
#define NTPUpdateIntervall 60000
NTPClient timeClient(ntpUDP, "0.pool.ntp.org", 0, NTPUpdateIntervall);
unsigned long ntpUpdateTimer = 0;

unsigned long getDataTimer = 0;
unsigned long updateIntervall = 5000;
String message;
bool timeValid = false;
bool lastTimeValid = false;
unsigned long heartbeat = 0;

void processCmdRemoteDebug() {
  String lastCmd = Debug.getLastCommand();
  if (lastCmd == "enableSleep") {
    debugA("* DeepSleep is enabled");
  }
  if (lastCmd == "disableSleep") {
    debugA("* DeepSleep is disabled");
  }
}

void startWIFI() {
  setCpuFrequencyMhz(80);
#ifdef debug
  Serial.println("startWIFI");
#endif
  wifiInit();
  IP_info();
}

void stopWIFI() {
  setCpuFrequencyMhz(10);
#ifdef debug
  Serial.println("stopWIFI");
#endif
  WiFi.disconnect();
}

void setup()
{
#ifdef debug
  Serial.begin(115200);
  Serial.println("Booting");
#endif
  sendMeasureIndex = (measureIndex - 1) & ((1 << MeasureIndexBits) - 1);
  sendState = started;

  btStop();

  wifiInit();       // get WiFi connected
  IP_info();

  setupOTA();
  String hostname = MQTTClientName;
  hostname += ".local";
  setupRemoteDebug(hostname.c_str());

  String cmds = "enableSleep - Enable Deep Sleep\n\rdisableSleep - Disable Deep Sleep";

  Debug.setHelpProjectsCmds(cmds);
  Debug.setCallBackProjectCmds(&processCmdRemoteDebug);

  MQTTClient.enableDebuggingMessages(debug); // Enable debugging messages sent to serial output
  MQTTClient.enableLastWillMessage("device/lastwill", MQTTClientName);

  // Setup MH-Z19 CO2 Sensor over serial interface
  mySerial.begin(BAUDRATE, SERIAL_8N1, RX_PIN, TX_PIN); // (ESP32 Example) device to MH-Z19 serial start
  myMHZ19.begin(mySerial);                                // *Serial(Stream) refence must be passed to library begin().
  myMHZ19.autoCalibration();                              // Turn auto calibration ON (OFF autoCalibration(false))

  //create a task that will be executed in the Task1code() function, with priority 1 and executed on core 0
  xTaskCreatePinnedToCore(
    TaskMeasure, /* Task function. */
    "Measure",     /* name of task. */
    10000,       /* Stack size of task */
    NULL,        /* parameter of the task */
    1,           /* priority of the task */
    &Task1,      /* Task handle to keep track of created task */
    0);          /* pin task to core 0 */
  delay(500);
}

// This function is called once everything is connected (Wifi and MQTT)
void onConnectionEstablished()
{
  esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
  MQTTClient.publish("device/online", "CO2Sensor");
  MQTTClient.subscribe("CO2Sensor", [](const String & payload) {
    debugD("Received message: %s", payload.c_str());

    if (payload == "getCommands") {
      debugD("Received get commands");
      String commands = "{\"commands\":[{\"cmd\":\"Intervall\",\"type\":\"integer\",\"min\":5000,\"max\":3600000,\"value\":";
      commands += updateIntervall;
      commands += "},{\"cmd\":\"Debug\",\"type\":\"bool\"}]}";
      MQTTClient.publish("CO2Sensor/commands", commands);
    } else if (payload == "info") {
      debugD("Received info command");
      char SensorVersion[5] = {0, 0, 0, 0, 0};
      myMHZ19.getVersion(SensorVersion);
      String info = "{\"version\":\"";
      info += SWVersion;
      info += "\",\"Sensor\":\"MH-Z19b V";
      info += SensorVersion;
      info += "\"}";
      MQTTClient.publish("CO2Sensor/info", info);
    } else {
      const int capacity = JSON_OBJECT_SIZE(3) + 2 * JSON_OBJECT_SIZE(1);
      StaticJsonDocument<capacity> doc;
      DeserializationError err = deserializeJson(doc, payload);
      if (err) {
        debugE("deserializeJson() failed with code %s", err.c_str());
      }

      if (doc.containsKey("Intervall")) {
        auto receivedVal = doc["Intervall"].as<unsigned long>();
        debugD("Received Intervall: %lu", receivedVal);
        if ((receivedVal >= 5000) && (receivedVal < 3600000)) {
          updateIntervall = receivedVal;
        }
      }
      if (doc.containsKey("Debug")) {
        disableSleep = doc["Debug"].as<bool>();
#ifdef debug
        Serial.print("disable Sleep = ");
        if (disableSleep) Serial.println("true");
        else Serial.println("false");
#endif
        debugD("Debug: disable Sleep = %s", disableSleep ? "true" : "false");
      }
    }
  });

  MQTTClient.subscribe("device", [](const String & payload) {
    if (payload == "scan") {
      debugD("Received device scan");
      MQTTClient.publish("device/scan", MQTTClientName);
    }
  });
  timeClient.begin();
  getDataTimer = millis();
  ntpUpdateTimer = millis();
}

String createSendString(Measurement measure) {
  String message = "";
  String strTime = "";
  strTime += measure.time;
  String milliseconds = "000";
  milliseconds += measure.ms;
  milliseconds = milliseconds.substring(milliseconds.length() - 3);
  strTime += milliseconds;

  // Publish a message to "mytopic/test"
  message = "[{\"name\":\"";
  message += MQTTClientName;
  if (measure.CO2Error == RESULT_OK ) {
    message += "\",\"field\":\"CO2\",\"value\":";
    message += measure.CO2;
  } else {
    message += "\",\"field\":\"CO2Error\",\"value\":";
    message += measure.CO2Error;
  }
  if (measure.CO2unlimitedError == RESULT_OK ) {
    message += ",\"CO2unlimited\":";
    message += measure.CO2unlimited;
  } else {
    message += ",\"CO2unlimitedError\":";
    message += measure.CO2unlimitedError;
  }
  if (measure.CO2RawError == RESULT_OK ) {
    message += ",\"CO2Raw\":";
    message += measure.CO2Raw;
  } else {
    message += ",\"CO2RawError\":";
    message += measure.CO2RawError;
  }
  if (measure.TempError == RESULT_OK ) {
    message += ",\"Temp\":";
    message += measure.Temp;
  } else {
    message += ",\"TempError\":";
    message += measure.TempError;
  }
  if (measure.AccuracyError == RESULT_OK ) {
    message += ",\"Accuracy\":";
    message += measure.Accuracy;
  } else {
    message += ",\"AccuracyError\":";
    message += measure.AccuracyError;
  }
  message += ",\"RSSI\":";
  message += WiFi.RSSI();
  message += ",\"time\":";
  message += strTime;
  message += "},";
  message += "{\"name\":\"";
  message += MQTTClientName;
  message += "\",\"field\":\"Heartbeat\",\"value\":";
  message += heartbeat++;
  message += ",\"time\":";
  message += strTime;
  message += "}]";
  return message;
}

bool sendMeasurement (unsigned int i) {
  message = createSendString(measures[i]);
  debugD("MQTT Publish: %s", message.c_str());
  if (MQTTClient.publish("sensors", message)) { // You can activate the retain flag by setting the third parameter to true)
#ifdef debug
    Serial.println("MQTT OK");
#endif
    return true;
  } else {
#ifdef debug
    Serial.println("MQTT Error");
#endif
    return false;
  }
}

void TaskMeasure (void * pvParameters) {
  const TickType_t xFrequency = 10;

  // Initialise the xLastWakeTime variable with the current time.
  TickType_t xLastWakeTime = xTaskGetTickCount ();
  for ( ;; )
  {
    // Wait for the next cycle.
    vTaskDelayUntil( &xLastWakeTime, pdMS_TO_TICKS(5000) );
    if (timeValid) {
      measures[measureIndex].CO2 = myMHZ19.getCO2(false);                             // Request CO2 (as ppm)
      measures[measureIndex].CO2Error = myMHZ19.errorCode;
      measures[measureIndex].CO2unlimited = myMHZ19.getCO2(true);                   // Request CO2 unlimited
      measures[measureIndex].CO2unlimitedError = myMHZ19.errorCode;
      measures[measureIndex].CO2Raw = myMHZ19.getCO2Raw();
      measures[measureIndex].CO2RawError = myMHZ19.errorCode;
      measures[measureIndex].Temp = myMHZ19.getTemperature(true);                    // Request Temperature as float (as Celsius)
      measures[measureIndex].TempError = myMHZ19.errorCode;
      measures[measureIndex].Accuracy = myMHZ19.getAccuracy();
      measures[measureIndex].AccuracyError = myMHZ19.errorCode;

      if (myMHZ19.errorCode != RESULT_OK) {
        debugE("Error from MHZ19: %u", myMHZ19.errorCode);
      }
      unsigned int ms = 0;
      measures[measureIndex].time = timeClient.getEpochTime(ms);
      measures[measureIndex].ms = ms;
      incMeasureIndex();
      debugV("Measure: %d", measureIndex);
#ifdef debug
      Serial.print("Measure");
      Serial.println(measureIndex);
#endif
    }
  }
}

void loop()
{
  MQTTClient.loop();
  if (WiFi.status() == WL_CONNECTED) {
    ArduinoOTA.handle();
    Debug.handle();
    if (millis() - ntpUpdateTimer >= 2000) {
      ntpUpdateTimer += 2000;

      if (timeClient.getEpochTime() < 1500000000) {
        debugE("not a valid time: %lu %s", timeClient.getEpochTime(), timeClient.getFormattedTime());
        timeValid = false;
      } else {
        timeValid = true;
      }

      if (timeValid) {
        if (!timeClient.update()) {
          debugE("update failed: %s", timeClient.getFormattedTime());
        }
      } else {
        if (!timeClient.forceUpdate()) {
          debugE("forceupdate failed: %s", timeClient.getFormattedTime());
        }
      }

      if (!lastTimeValid && timeValid) {
        debugE("Got new Time: %s", timeClient.getFormattedTime());
      }
      lastTimeValid = timeValid;
    }

    if (sendState == tostart) {
      stoptime = millis() + 5000;
      sendState = startdelay;
    }
    if (sendState == startdelay) {
      if (millis() > stoptime) {
        sendState = started;
      }
    }
    if (sendState == started) {
      if (MQTTClient.isConnected() && (countStoredMeasurements() > 0)) {
#ifdef debug
        Serial.print("StoredMeasurements: ");
        Serial.println(countStoredMeasurements());
#endif
        sendMeasurement(sendMeasureIndex);
        incSendMeasureIndex();
      }
    }
  }
  if ((sendState == stopped) && (countStoredMeasurements() > (maxMeasurements / 2))) {
    sendState = tostart;
#ifdef debug
    Serial.println("StartSending");
#endif
    if (WiFi.status() != WL_CONNECTED) {
      startWIFI();
    }
  }
  if ((sendState == started) && (countStoredMeasurements() < 1)) {
    if (timeValid) {
      sendState = tostop;
#ifdef debug
      Serial.println("toStop");
#endif
      stoptime = millis() + 5000;
    }
  }
  if (sendState == tostop) {
    if (millis() > stoptime) {
      sendState = stopped;
#ifdef debug
      Serial.println("StopSending");
#endif
      if (!disableSleep) {
        stopWIFI();
      }
    }
  }
}
