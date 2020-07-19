/*
   Header for infrastructe
   - OTA
   - Debugger
   - SmartConfig

   MIT License

   Copyright (c) 2019 Joao Lopes

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in all
   copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.


*/
#ifndef __BasicInfrastructure
#define __BasicInfrastructure

#ifndef disableRemoteDebug
#include "RemoteDebugCfg.h"
#include <RemoteDebug.h>
RemoteDebug Debug;
void setupRemoteDebug(const char* hostname) {
    Debug.begin(hostname);

}
#endif

#ifndef disableSmartConfig

#include <esp_wifi.h>
#include <Preferences.h>  // WiFi storage

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

  WiFi.mode( WIFI_STA );       //Init WiFi, start SmartConfig
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
   WiFi.mode(WIFI_STA);   // required to read NVR before WiFi.begin()

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
   if (WiFi.status() != WL_CONNECTED) {
        initSmartConfig(); 
        delay( 3000);
        ESP.restart();   // reboot with wifi configured
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
#endif

#ifndef disableOTA
#include <ArduinoOTA.h>


void setupOTA() {

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
    debugD("Progress: %u%%\r", (progress / (total / 100)));
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
}
#endif
#endif
