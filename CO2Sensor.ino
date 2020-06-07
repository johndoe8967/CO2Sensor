
/*
  SimpleMQTTClient.ino
  The purpose of this exemple is to illustrate a simple handling of MQTT and Wifi connection.
  Once it connects successfully to a Wifi network and a MQTT broker, it subscribe to a topic and send a message to it.
  It will also send a message delayed 5 seconds later.
*/
#include "FS.h"
#include <esp_wifi.h>
#include "esp_system.h"
#include <Preferences.h>  // WiFi storage
#include "WiFi.h"
#include "EspMQTTClient.h"
#include <NTPClient.h>
#include "MHZ19.h"    
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <RemoteDebugger.h>

RemoteDebug Debug;

void wifiInit();
void IP_info();

// Variables for WIFI SmartConfig auto reconnect
  const  char* rssiSSID;       // NO MORE hard coded set AP, all SmartConfig
  const  char* password;
  String PrefSSID, PrefPassword;  // used by preferences storage

  int UpCount = 0;
  String getSsid;
  String getPass;
  String  MAC;
  Preferences preferences;  // declare class object

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

bool connected = false;
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

  // Setup OTA 
  ArduinoOTA.setHostname("CO2Sensor");
  ArduinoOTA.setPassword("CO2Sensor");
  ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
        type = "filesystem";

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type);
      debugD("Start updating");
    })
    .onEnd([]() {
      Serial.println("\nEnd");
      debugD("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
      debugD("Progress: %u%%\r",(progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      debugD("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });
  ArduinoOTA.begin();
  Debug.begin("CO2Sensor.local");

  MQTTClient.enableDebuggingMessages(); // Enable debugging messages sent to serial output
  MQTTClient.enableLastWillMessage("CO2Sensor/lastwill", "I am going offline");  // You can activate the retain flag by setting the third parameter to true

  // Setup MH-Z19 CO2 Sensor over serial interface
  mySerial.begin(BAUDRATE, SERIAL_8N1, RX_PIN, TX_PIN); // (ESP32 Example) device to MH-Z19 serial start   
  myMHZ19.begin(mySerial);                                // *Serial(Stream) refence must be passed to library begin(). 
  myMHZ19.autoCalibration();                              // Turn auto calibration ON (OFF autoCalibration(false))
}

// This function is called once everything is connected (Wifi and MQTT)
void onConnectionEstablished()
{
  // Subscribe to "mytopic/test" and display received message to Serial
  MQTTClient.subscribe("CO2Sensor/Update", [](const String & payload) {
    debugD("Received message: %s",payload.c_str());
    
    const int capacity = JSON_OBJECT_SIZE(3) + 2*JSON_OBJECT_SIZE(1); 
    StaticJsonDocument<capacity> doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) { 
      debugD("deserializeJson() failed with code %s",err.c_str());
    }

    if (doc.containsKey("Intervall")) {
      auto receivedVal = doc["Intervall"].as<unsigned long>();
      debugD("Received Intervall: %lu",receivedVal);
      if ((receivedVal > 1000) && (receivedVal < 3600000)) {
        updateIntervall = receivedVal;
      }
    }
    if (doc.containsKey("Debug")) {
      MQTTClient.enableDebuggingMessages(doc["Debug"].as<bool>());
      debugD("Received MQTTDebug over Serial");
    }
  });

  // Subscribe to "mytopic/wildcardtest/#" and display received message to Serial
  MQTTClient.subscribe("mytopic/wildcardtest/#", [](const String & topic, const String & payload) {
    Serial.println(topic + ": " + payload);
  });

  timeClient.begin();
  connected = true;
}

void loop()
{
  ArduinoOTA.handle();
  Debug.handle();
  MQTTClient.loop();

  timeValid = timeClient.update();
  if (!lastTimeValid && timeValid) {
    timeValid = true;
    debugA("Got new Time: %s",timeClient.getFormattedTime());
  }
  lastTimeValid = timeValid;
  
  if (millis() - getDataTimer >= updateIntervall) {
    getDataTimer = millis();  
    
    CO2 = myMHZ19.getCO2(true);                             // Request CO2 (as ppm)
    CO2uncorrected = myMHZ19.getCO2(false);        
    Temp = myMHZ19.getTemperature();                     // Request Temperature (as Celsius)
  
    if (connected) {
      String strTime = "";
      unsigned int ms = 0;
      strTime += timeClient.getEpochTime(ms);
      String milliseconds = "000";
      milliseconds += ms;
      milliseconds = milliseconds.substring(milliseconds.length() - 3);
      strTime += milliseconds;
      //    Serial.println(milliseconds);
      // Publish a message to "mytopic/test"
      message = "[{\"name\":\"CO2Sensor\",\"field\":\"CO2\",\"value\":";
      message += CO2;
      message += ",\"Temp\":";
      message += Temp;
      
     
      message += ",\"time\":";
      message += strTime;
      message += "},";
      message += "{\"name\":\"CO2Sensor\",\"field\":\"CO2uncorrected\",\"value\":";
      message += CO2uncorrected;
      message += ",\"time\":";
      message += strTime;
      message += "}]";
      debugD("MQTT Publish: %s",message.c_str());
      MQTTClient.publish("sensors", message); // You can activate the retain flag by setting the third parameter to true
    }
  }
}




// Requires; #include <esp_wifi.h>
// Returns String NONE, ssid or pass arcording to request 
// ie String var = getSsidPass( "pass" );
String getSsidPass( String s )
{
  String val = "NONE";  // return "NONE" if wrong key sent
  s.toUpperCase();
  if( s.compareTo("SSID") == 0 )
  {
     wifi_config_t conf;
     esp_wifi_get_config( WIFI_IF_STA, &conf );
     val = String( reinterpret_cast<const char*>(conf.sta.ssid) );
  }
  if( s.compareTo("PASS") == 0 )
  {
     wifi_config_t conf;
     esp_wifi_get_config( WIFI_IF_STA, &conf );
     val = String( reinterpret_cast<const char*>(conf.sta.password) );
  }
 return val;
}

// match WiFi IDs in NVS to Pref store,  assumes WiFi.mode(WIFI_AP_STA);  was executed
bool checkPrefsStore()   
{
    bool val = false;
    String NVssid, NVpass, prefssid, prefpass;

    NVssid = getSsidPass( "ssid" );
    NVpass = getSsidPass( "pass" );

    // Open Preferences with my-app namespace. Namespace name is limited to 15 chars
    preferences.begin("wifi", false);
        prefssid  =  preferences.getString("ssid", "none");     //NVS key ssid
        prefpass  =  preferences.getString("password", "none"); //NVS key password
    preferences.end();

    if( NVssid.equals(prefssid) && NVpass.equals(prefpass) )
      { val = true; }

  return val;
}

// optionally call this function any way you want in your own code
// to remap WiFi to another AP using SmartConfig mode.   Button, condition etc.. 
void initSmartConfig() 
{
   // start LED flasher
  int loopCounter = 0;

  WiFi.mode( WIFI_AP_STA );       //Init WiFi, start SmartConfig
  Serial.printf( "Entering SmartConfig\n" );

  WiFi.beginSmartConfig();

  while (!WiFi.smartConfigDone()) 
  {
     // flash led to indicate not configured
     Serial.printf( "." );
     if( loopCounter >= 40 )  // keep from scrolling sideways forever
     {
         loopCounter = 0;
         Serial.printf( "\n" );
     }
     delay(600);
    ++loopCounter;
  }
  loopCounter = 0;

  // stopped flasher here

   Serial.printf("\nSmartConfig received.\n Waiting for WiFi\n\n");
   delay(2000 );
    
  while( WiFi.status() != WL_CONNECTED )      // check till connected
  { 
    delay(500);
  }
  IP_info();  // connected lets see IP info

  preferences.begin("wifi", false);      // put it in storage
  preferences.putString( "ssid"         , getSsid);
  preferences.putString( "password", getPass);
  preferences.end();

  delay(300);
}  // END SmartConfig()

void wifiInit()  // 
{
   WiFi.mode(WIFI_AP_STA);   // required to read NVR before WiFi.begin()

   // load credentials from NVR, a little RTOS code here
   wifi_config_t conf;
   esp_wifi_get_config(WIFI_IF_STA, &conf);  // load wifi settings to struct comf
   rssiSSID = reinterpret_cast<const char*>(conf.sta.ssid);
   password = reinterpret_cast<const char*>(conf.sta.password);

   // Open Preferences with "wifi" namespace. Namespace is limited to 15 chars
   preferences.begin("wifi", false);
   PrefSSID      =  preferences.getString("ssid", "none");      //NVS key ssid
   PrefPassword  =  preferences.getString("password", "none");  //NVS key password
   preferences.end();

   // keep from rewriting flash if not needed
   if( !checkPrefsStore() )      // see is NV and Prefs are the same
   {              // not the same, setup with SmartConfig
      if( PrefSSID == "none" )  // New...setup wifi
      {
        initSmartConfig(); 
        delay( 3000);
        ESP.restart();   // reboot with wifi configured
      }
   } 

   // I flash LEDs while connecting here

   WiFi.begin( PrefSSID.c_str() , PrefPassword.c_str() );

   int WLcount = 0;
   while (WiFi.status() != WL_CONNECTED && WLcount < 200 ) // can take > 100 loops depending on router settings
   {
     delay( 100 );
     Serial.printf(".");
     ++WLcount;
   }
  delay( 3000 );

  //  stop the led flasher here

  }  // END wifiInit()


void IP_info()
{
   getSsid = WiFi.SSID();
   getPass = WiFi.psk();

   Serial.printf( "\n\n\tSSID\t%s\n", getSsid.c_str() );
   Serial.printf( "\tPass\t %s\n", getPass.c_str() ); 
   Serial.print( "\n\n\tIP address:\t" );  Serial.print(WiFi.localIP() );
   Serial.print( " / " );
   Serial.println( WiFi.subnetMask() );
   Serial.print( "\tGateway IP:\t" );  Serial.println( WiFi.gatewayIP() );
   Serial.print( "\t1st DNS:\t" );     Serial.println( WiFi.dnsIP() );
   Serial.printf( "\tMAC:\t\t%s\n", MAC.c_str() );
}
