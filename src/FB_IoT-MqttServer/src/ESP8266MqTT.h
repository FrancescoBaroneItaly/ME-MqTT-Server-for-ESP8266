/*
  Francesco Barone 2017(c)
  <barone_f@yahoo.com>
  ME PROJECT
  mqttserver.h - Config ESP to Crumb IOT Platform
*/

#ifndef MQTTSERVER_h
#define MQTTSERVER_h

#define VERSION 2
#define PATCH 011
// 1.0   - implementazione base
// 2.0   - BETA TESTER
// 2.010 - aggiunta security
// 2.011 - fixed client lost during transmission 28.07
// 3.0   - RELEASE

#if defined(ESP8266)
  #include <pgmspace.h>
#else
  #include <avr/pgmspace.h>
#endif

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266WebServer.h>
#include "ESP8266WiFiAP.h"
#include <ESP8266HTTPClient.h>
#include <ESP8266TrueRandom.h>

#include <Wire.h>
#include "RTClib.h"
#include <ArduinoJson.h>
#include <Hash.h>
#include <EEPROM.h>

#include <PubSubClient.h>
#include <rBase64.h>

#include <SoftwareSerial.h>
//#include <SD.h>
#include <FS.h>

#if defined(__SAM3X8E__)
    #undef __FlashStringHelper::F(string_literal)
    #define F(string_literal) string_literal
#endif

extern "C" {

  #include "user_interface.h"
  }

#include <SPI.h>

#ifdef ESP8266
	#include <functional>
	#define MQTT_CALLBACK_SIGNATURE std::function<boolean(char*, uint8_t*, uint32_t)> mqtt_callback
#endif

class ME
{
  public:

	ME(String MqTT_Server_InstanceID);

	//BASE
	void begin(HardwareSerial *param_serial);
	void loop();

	//LOG
	void setloglevel(uint8_t level);
	void log(uint8_t level, String payload);

	//WIFI
	boolean WIFI_connected=false;
	boolean WIFI_request=false;

	//WWW
	void handleNotFound_internal();
	void handle_set_config_internal();
	void handle_admin_user_internal();
	void handle_index_internal();
	void handle_login_internal();
	//void handle_callback_internal();

	//SERVER
	ME& setCallback(MQTT_CALLBACK_SIGNATURE);
	boolean subscribe(String topic_filter, boolean CleanSession);
	boolean unsubscribe(String topic_filter);
	void stop_callback();
	void play_callback();
	boolean publish(const char* topic, const char* payload);
    boolean publish(const char* topic, const char* payload, boolean retained);
    boolean publish(const char* topic, const uint8_t * payload, unsigned int plength);
    boolean publish(const char* topic, const uint8_t * payload, unsigned int plength, boolean retained);


	//MqTT DEF
	#define MAX_DATA_LEN 512

  private:

	//BASE
	HardwareSerial *serial;
	String MqTT_Server_InstanceID;

	//BASE
	String ssid     = "ESPBLANK";
	String password = "alfafrancesco";

	//FS
	boolean hasSPIFFS=false;

	struct control_packet_connect {

		boolean username_flag=false;
		boolean password_flag=false;
		String ClientID="";
		boolean willretain_flag=false;
		int willqos_flag=0x00;
		boolean will_flag=false;
		boolean cleansession_flag=false;
		int protocol_version=4;
		long keep_alive_time=0;
		};

	control_packet_connect C_CONNECT;

	//WWW
	String prefix = "";
	String index_page = "";
	ESP8266WebServer www_server = ESP8266WebServer(80);
	bool loadFromSPIFFS(String path);
	bool is_authentified();

	//EEPROM
	void save_field_EEPROM(int MEM_POS, String field, int field_len);
	String load_field_EEPROM(int MEM_POS, int field_len);

	//CONFIG
	int getConfigStatus();
	void load_cfg();
	String ADMIN_USER = "";
	String ADMIN_PASSWORD = "";
	#define DEFAULT_ADMIN_USER "admin"
	#define DEFAULT_ADMIN_PASSWORD "passw0rd" //con lo zero :-)
	boolean USER_AUTH=false;

	//STATE
	int STATE=0;
	int STATE_CALL=0;
	#define NOP 9999
	#define START 10
	#define CHECK_CONFIG 20
	#define WAIT_CONFIG 30
	#define CONFIG_CORRUPTED 21
	#define CONFIGURED 40
	#define MQTT_LISTEN 50
	#define START_SERVER 60
	#define MQTT_EVENT 70
	#define MQTT_CONNECT 80
	#define MQTT_SUBSCRIBE 90
	#define MQTT_PINGREQ 100
	#define MQTT_PUBLISH 110
	#define MQTT_PROCESS 120
	#define UPDATE_STATS 130
	#define CLEAN_FS 140
	#define MQTT_SRVTIME 150
	#define SET_SRVTIME 151
	#define MSG_EXPIRETIME 160
	#define CLEAN_EXPIRED 170

	//VAR
	int CONFIG_STATUS=0;
	String AP_SSID = "";
	String AP_PASSWORD = "";
	String IP1 = "192";
	String IP2 = "168";
	String IP3 = "4";
	String IP4 = "1";
	String GW_IP1 = "192";
	String GW_IP2 = "168";
	String GW_IP3 = "4";
	String GW_IP4 = "1";
	String SB_IP1 = "255";
	String SB_IP2 = "255";
	String SB_IP3 = "255";
	String SB_IP4 = "0";
	String SERVER_INSTANCE_NAME = "NONAME";
	String SERVER_SERIALNUMBER = "";
	int MQTT_PORT = 1883;
	#define ME_CONFIGURED 1
	#define ME_UNCONFIGURED 9
	boolean WWW_RUNNING=false;
	boolean slowDownInternalCallback=false;
	long t_slowDownInternalCallback=0;
	int c_slowDownInternalCallback=0;

	//TIMERS
	void init_timers();
	boolean add_timer(String id);
	void start_timer(String id);
	void fire_timer(String id);
	boolean trigger_timer(String id, long timeout);
	#define TIMERS 4
	String timer_id[TIMERS];
	long timer_ms[TIMERS];

	//LOG
	#define SUPER 3
	#define TRACE 2
	#define DEBUG 1
	#define PROD 0
	uint8_t LOG_LEVEL=TRACE;

	//SERVER
	#define MAX_SRV_CLIENTS 16
	#define SESSION_FREE    0
	#define SESSION_ALLOCATED    1
	#define MAX_EXPIRE_MSGTIME 20 //minutes
	WiFiServer *mqtt_server;
	WiFiClient mqtt_clients[MAX_SRV_CLIENTS];
	void freeSession(int idx);
	boolean allocSession(int c, String ClientID, long KeepAliveTime, boolean CleanSession, String UserName, String Password);
	boolean sendData(int idx,uint8_t DATA[], int n);
	boolean checkMatchTopic(String topic1, String topic2);
	uint8_t c;
	int idx;
	boolean event_interrupt=false;
	int subsid_inc=0;
	int msgid_inc=0;
	long t_check;
	MQTT_CALLBACK_SIGNATURE;
	#define MAX_LISTENER 16
	String topic_listener[MAX_LISTENER];
	boolean storeMessage(String ClientID, String topic_filter, String payload, uint8_t dup, uint8_t qos, uint8_t retain);
	void CleanClientSession(String CLIENTID);
	boolean internal_callback_paused=false;
	long inc_uptime=0;
	long uptime_ms=0;
	void updateConfigFiles();
	//AUTH
	boolean connectAuth(String UserName, String Password);
	boolean isAdminSession(String UserName, String Password);

	//OPTIMIZE WORKLOAD
	int offset_process=0;
	#define MAX_PROCESS 32

	//STATS
	long t_stats;
	long in_msg=0;
	long out_msg=0;

	#define MQTT_CHECK_TIME 30
	#define MQTT_STATS 60

	struct mqtt_server_session {

	  int State=SESSION_FREE;
	  boolean CleanSession;
	  String ClientID;
	  long KeepAliveTime;
	  long AliveTime;
	  String UserName;
	  String Password;
	  boolean isAdminSession=false;
	  };

	mqtt_server_session SERVER_SESSION[MAX_SRV_CLIENTS];

	#define MAX_PACKET_LEN 1024
	uint8_t IN_PACKET[MAX_PACKET_LEN];

	//MQTT
	#define MQTTCONNECT     1 << 4  // Client request to connect to Server
	#define MQTTCONNACK     2 << 4  // Connect Acknowledgment
	#define MQTTPUBLISH     3 << 4  // Publish message
	#define MQTTPUBACK      4 << 4  // Publish Acknowledgment
	#define MQTTPUBREC      5 << 4  // Publish Received (assured delivery part 1)
	#define MQTTPUBREL      6 << 4  // Publish Release (assured delivery part 2)
	#define MQTTPUBCOMP     7 << 4  // Publish Complete (assured delivery part 3)
	#define MQTTSUBSCRIBE   8 << 4  // Client Subscribe request
	#define MQTTSUBACK      9 << 4  // Subscribe Acknowledgment
	#define MQTTUNSUBSCRIBE 10 << 4 // Client Unsubscribe request
	#define MQTTUNSUBACK    11 << 4 // Unsubscribe Acknowledgment
	#define MQTTPINGREQ     12 << 4 // PING Request
	#define MQTTPINGRESP    13 << 4 // PING Response
	#define MQTTDISCONNECT  14 << 4 // Client is Disconnecting
	#define MQTTReserved    15 << 4 // Reserved
};

#endif