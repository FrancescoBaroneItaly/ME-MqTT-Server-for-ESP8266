/*
 * Author: Francesco Barone
 * MqTT Server simple sketch
 * 18.07.2017 - release 1
 */
 
#include "ESP8266MqTT.h";
#include "ESP.h"

#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <PubSubClient.h> // https://github.com/knolleary/pubsubclient/releases/tag/v2.3
#include <ArduinoJson.h> // https://github.com/bblanchon/ArduinoJson/releases/tag/v5.0.7

//-------- Customise these values -----------
const char* ssid = "Mobile";
const char* password = "alfafrancesco";
//-------- Customise the above values --------

WiFiClient wifiClient;
//--------------------------------------------

ME me("MqTT");

ESP8266WiFiMulti WiFiMulti;  

int STATE=0;
const int _START=10;
const int _WIFI_IP=20;
const int _WIFI_CONNECT=30;
const int _LOOP_APP=40;
boolean WIFI_connected;

  
boolean mqtt_callback (char* queue, byte* payload, unsigned int length){

  String data = String((char *)payload);
  Serial.println("**********************"+String(queue)+"> ["+data+"]");
  }
  
void setup() {
   
  Serial.begin(115200);

  //start
  me.begin(&Serial);
  
  //add callback
  me.setCallback(mqtt_callback);
  me.subscribe("/test/temperature",false);    
  
  WiFiMulti.addAP(ssid,password);  
    
  //0=PROD
  //me.setloglevel(PROD);

  STATE=START;
  }

void loop() {

  //process mqtt
  me.loop();
  
  switch(STATE){
    
    case _START:

      WIFI_connected=false;
      
      if( WiFiMulti.run() == WL_CONNECTED) {
        
        WIFI_connected=true;
        STATE=_WIFI_IP;
        }
        
      break;

    case _WIFI_IP:

      Serial.print("Ready at ");
      Serial.println(WiFi.localIP());
      
      STATE=_WIFI_CONNECT;
      break;
      
    case _WIFI_CONNECT:

      if( WiFiMulti.run() != WL_CONNECTED) {

        WIFI_connected=false;  
        STATE=_START;
      }else{

        STATE=_LOOP_APP;
        }
        
      break;
    
    case _LOOP_APP:

      /*  
       *   
       *   my logic here
       */
       
      STATE=_WIFI_CONNECT;
        
      break;
      
    default:
      break;
    }
    
  
}


