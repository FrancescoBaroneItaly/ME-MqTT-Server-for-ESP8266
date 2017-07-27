/*
  Francesco Barone 2017(c)
  <barone_f@yahoo.com>
  ME PROJECT

  MQTTESP.cpp
*/

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

#include "ESP8266MqTT.h"

ME *_me;

//callback
void int_wifi_callback(WiFiEvent_t event ) {

    {
    char tmp[32];
    sprintf(tmp,"WiFi Event = %d",event);
    _me->log(SUPER,tmp);
    }

    switch(event) {
        case WIFI_EVENT_STAMODE_GOT_IP:

            _me->log(TRACE,"[EVENT]WiFi connected");
			_me->WIFI_connected=true;
            //_crumb->WIFI_request=false;
            break;

        case WIFI_EVENT_STAMODE_DISCONNECTED:

            _me->log(TRACE,"[EVENT]WiFi lost connection");
            //if(_crumb->isSTA_EN()){
              //_crumb->WIFI_connected=false;
              //_crumb->WIFI_request=false;
              //_crumb->WIFI_action=false;
            //  }
            break;
    }
}

void handleNotFound(){

	_me->handleNotFound_internal();
	}

void handle_login(){

	_me->handle_login_internal();
	}

void handle_index(){

	_me->handle_index_internal();
	}

void handle_set_config(){

	_me->handle_set_config_internal();
	}

void handle_admin_user(){

	_me->handle_admin_user_internal();
	}

//TIMER
void ME::init_timers(){

  for(int i=0;i<TIMERS;i++){

    timer_id[i]="";//BLANK
    timer_ms[i]=-1;//OFF
    }
  }

boolean ME::add_timer(String id){

  boolean added=false;
  boolean full=true;
  boolean found=false;
  for(int i=0;i<TIMERS;i++){

    if(strcmp(timer_id[i].c_str(),id.c_str())==0)found=true;
    }

  for(int i=0;i<TIMERS && !found;i++){

    if(strcmp(timer_id[i].c_str(),"")==0)full=false;
    }

  for(int i=0;i<TIMERS && !full && !added;i++){

    if(strcmp(timer_id[i].c_str(),"")==0){
      timer_id[i]=id;
      timer_ms[i]=-1;
      added=true;
      }
    }

  return added;
  }

void ME::start_timer(String id){

   for(int i=0;i<TIMERS;i++){

    if(strcmp(timer_id[i].c_str(),id.c_str())==0)timer_ms[i]=millis();
    }
  }

void ME::fire_timer(String id){

   for(int i=0;i<TIMERS;i++){

    if(strcmp(timer_id[i].c_str(),id.c_str())==0)timer_ms[i]=0;
    }
  }

boolean ME::trigger_timer(String id, long timeout){

  boolean triggered=false;
  for(int i=0;i<TIMERS;i++){

    if(strcmp(timer_id[i].c_str(),id.c_str())==0){

      if( (millis()-timer_ms[i]) > timeout || timer_ms[i]==0){
        timer_ms[i]=millis();
        triggered=true;
        }
      }
    }

  return triggered;
  }

ME::ME(String MqTT_Server_InstanceID){

	this->MqTT_Server_InstanceID = MqTT_Server_InstanceID;
	}

void ME::begin(HardwareSerial *param_serial){

	//EEPROM
	EEPROM.begin(4096);

	serial = param_serial;
	log(TRACE,"");

	char temp[80];
	sprintf(temp,"The module has started Firmware=12K16E%d.%d Memory=%u",VERSION,PATCH,ESP.getFreeHeap());
	log(TRACE,temp);

	sprintf(temp,"Server Instance=%s",MqTT_Server_InstanceID.c_str());
	log(TRACE,temp);

	log(TRACE,"Set WIFI off");
	WIFI_connected=false;

	//WIFI DEFAULT
	WiFi.persistent(false);
	WiFi.mode(WIFI_OFF);
	WiFi.onEvent(int_wifi_callback);

	//USE SPIFFS
	boolean spiffs = SPIFFS.begin();
	if(spiffs){

		hasSPIFFS=true;
		log(TRACE,"SPIFFS initialized");
		}

	//LOAD CFG
	load_cfg();

	//INIT TIMERS
	log(TRACE,"Init Timers");
	if(add_timer("CONFIG"))log(TRACE,">Timer CONFIG is ok");
	if(add_timer("UPTIME"))log(TRACE,">Timer UPTIME is ok");
	if(add_timer("PROCESS_EXPIRE"))log(TRACE,">Timer PROCESS_EXPIRE is ok");

	//Server Instance Time
	start_timer("UPTIME");
	inc_uptime=millis();

	start_timer("CONFIG");
	start_timer("PROCESS_EXPIRE");

	//CLEAN LISTENER
	for(int i=0;i<MAX_LISTENER;i++){
		topic_listener[i]="";
		}

	log(TRACE,"Setup Completed");

	STATE=START;

	_me = this;
	}

void ME::loop(){

	if(WWW_RUNNING)www_server.handleClient();

	switch(STATE){

		case START:

			log(TRACE,"Module is started");
			STATE=CHECK_CONFIG;
			break;

		case CHECK_CONFIG:

			log(TRACE,"Check config");
			STATE=CONFIG_CORRUPTED;

			//Attiva il webserver
			if(CONFIG_STATUS==ME_CONFIGURED){

				if(hasSPIFFS){

					updateConfigFiles();
					}

				log(TRACE,"Module is configured, Start web server");
				prefix = "/www";
				index_page="index.htm";

				//default
				www_server.on("/login", handle_login);
				www_server.on("/action/admin_user", handle_admin_user);

				#if defined(ARDUINO_ESP8266_ESP01)
					log(TRACE,">www for ESP01 Module");
					www_server.on("/", handle_index);
					www_server.on("/index.htm", handle_index);
				#else
					www_server.onNotFound(handleNotFound);
				#endif

				//here the list of headers to be recorded
				const char * headerkeys[] = {"User-Agent","Cookie"} ;
				size_t headerkeyssize = sizeof(headerkeys)/sizeof(char*);
				//ask server to track these headers
				www_server.collectHeaders(headerkeys, headerkeyssize );

				www_server.begin();

				WWW_RUNNING=true;

				STATE=CONFIGURED;
				}

			if(CONFIG_STATUS==ME_UNCONFIGURED){

				log(TRACE,"Start web server to config");
				prefix = "/www";
				index_page="cfg_index.htm";

				www_server.on("/action/set_config", handle_set_config);

				#if defined(ARDUINO_ESP8266_ESP01)
					log(TRACE,">www for ESP01 Module");
					www_server.on("/", handle_index);
					www_server.on("/index.htm", handle_index);
				#else
					www_server.onNotFound(handleNotFound);
				#endif

				www_server.begin();

				WWW_RUNNING=true;

				STATE=WAIT_CONFIG;
				}

			break;

		case WAIT_CONFIG:

			if(trigger_timer("CONFIG",2000)){

				if( load_field_EEPROM(0,1)=="1"){

					log(TRACE,"Proceed as configured");
					STATE=CONFIGURED;
					}
				}

			break;

		case CONFIGURED:

			if(hasSPIFFS){

				log(TRACE,"List previous SUBSCRIPTIONS");

				fs::Dir dir_data = SPIFFS.openDir("/subs");
				while (dir_data.next()) {

					fs::File f = dir_data.openFile("r");

					String fname = dir_data.fileName();
					log(TRACE,"Found "+fname);

					if(!fname.startsWith("/subs/CS")){

						//remove invalid file here
						f.close();
						SPIFFS.remove(fname);

					}else{

						{
						char temp[32];
						sprintf(temp,"Subscription> %s - %d",fname.c_str(),f.size());
						log(TRACE,temp);
						}

						f.close();
						}

					}

				dir_data = SPIFFS.openDir("/topic");
				while (dir_data.next()) {

					fs::File f = dir_data.openFile("r");

					String fname = dir_data.fileName();
					//log(TRACE,"Found "+fname);

					{
					char temp[32];
					sprintf(temp,"Message file> %s - %d",fname.c_str(),f.size());
					log(TRACE,temp);
					}
					f.close();
					}
				}

			STATE=CLEAN_FS;
			STATE_CALL=START_SERVER;
			break;

		case CLEAN_FS:
			{
			log(TRACE,"Clean invalid fs files");

			fs::Dir dir_data = SPIFFS.openDir("/topic");
			while (dir_data.next()) {

				fs::File f = dir_data.openFile("r");

				String msg_filename="";
				String fname = dir_data.fileName();
				//log(TRACE,"Found "+fname);

				if(fname.endsWith(".cd")){

					String csv[3];
					int counter = 0;
					int lastIndex = 0;

					String processline = fname+"/";

					//il primo carattere è /
					for (int i=1; i<processline.length(); i++) {

						if (processline.substring(i, i+1) == "/") {

							if(counter<3)csv[counter] = processline.substring(lastIndex, i);

							lastIndex = i + 1;
							counter++;
							}
						}

					if(counter>=2){

						msg_filename=csv[0]+"/"+csv[1];
						}
					}

				f.close();

				if(msg_filename!=""){

					log(TRACE,"Check "+msg_filename);
					if(!SPIFFS.exists(msg_filename+".msg") || !SPIFFS.exists(msg_filename+".st")){

						log(TRACE,">remove files");
						SPIFFS.remove(fname);
						SPIFFS.remove(msg_filename+".msg");
						SPIFFS.remove(msg_filename+".st");
						SPIFFS.remove(msg_filename);
						}
					}
				}

			}
			STATE=STATE_CALL;
			break;

		case UPDATE_STATS:
			{
			log(TRACE,"Update Server Stats");

			if(hasSPIFFS){

				int subs=0;
				int clients=0;

				fs::Dir dir_data = SPIFFS.openDir("/subs");
				while (dir_data.next()) {

					fs::File f = dir_data.openFile("r");

					String fname = dir_data.fileName();

					if(fname.startsWith("/subs/CS"))subs++;

					f.close();
					}

				for(int i=0;i<MAX_SRV_CLIENTS;i++){
					if (mqtt_clients[i] && mqtt_clients[i].connected() && SERVER_SESSION[i].State==SESSION_ALLOCATED )clients++;
					}

				{
				char temp[32];
				sprintf(temp,"Stats [%d][%d]",clients,subs);
				log(TRACE,temp);
				}

				fs::File f = SPIFFS.open("/www/data/stats.val", "w");
				f.println(in_msg);
				f.println(out_msg);
				f.println(clients);
				f.println(subs);
				f.close();

				//incremental show
				//in_msg=0;
				//out_msg=0;
				}
			}

			STATE=STATE_CALL;
			break;

		case START_SERVER:

			log(TRACE,"Start Server Instance");
			mqtt_server = new WiFiServer(MQTT_PORT);

			mqtt_server->begin();
			mqtt_server->setNoDelay(true);

			for(int i=0;i<MAX_SRV_CLIENTS;i++){

				freeSession(i);
				}

			STATE=UPDATE_STATS;
			STATE_CALL=SET_SRVTIME;
			break;

		case SET_SRVTIME:

			STATE=MQTT_SRVTIME;
			STATE_CALL=CLEAN_EXPIRED;
			break;

		case CLEAN_EXPIRED:

			STATE=MSG_EXPIRETIME;
			STATE_CALL=MQTT_LISTEN;
			break;

		case MQTT_LISTEN:

			if (mqtt_server->hasClient()){

				boolean connected=false;

				for(c = 0; c < MAX_SRV_CLIENTS && !connected; c++){
				  //find free/disconnected spot
				  if (!mqtt_clients[c] || !mqtt_clients[c].connected()){

					if(mqtt_clients[c]) mqtt_clients[c].stop();
					mqtt_clients[c] = mqtt_server->available();

					freeSession(c);
					//continue;
					connected=true;
				  }
				}

				if(!connected){
					//no free/disconnected spot so reject

					WiFiClient mqtt_client = mqtt_server->available();
					mqtt_client.stop();
					}
				}

			//check clients for data
  		    for(c = 0; c < MAX_SRV_CLIENTS; c++){

				if (mqtt_clients[c] && mqtt_clients[c].connected()){

					if(mqtt_clients[c].available()){

						wdt_disable();
						{
						char temp[32];
						sprintf(temp,"Data from Client %d",c);
						log(TRACE,temp);
						}

						//CLEAN
						for(int y=0;y<MAX_PACKET_LEN;y++)IN_PACKET[y]=0;

						//receive data
						idx=0;

						while(mqtt_clients[c].available()){

							uint8_t recv = mqtt_clients[c].read();
							if(idx<MAX_PACKET_LEN){

								IN_PACKET[idx]=recv;
								idx++;
								}

							}

						//log
						{
						String temp = "Recv[";
						for(int i=0;i<idx;i++){
							char d[8];
							sprintf(d," %x",IN_PACKET[i]);
							temp+=String(d);
							}
						temp+=" ]";
						log(TRACE,temp);
						}

						mqtt_clients[c].flush();

						wdt_enable(20000);

						//Process data
						STATE_CALL=MQTT_LISTEN;
						STATE=MQTT_EVENT;
						return;//exit from loop
						}
					}
				}

			if(trigger_timer("UPTIME",60000)){

				STATE_CALL=MQTT_LISTEN;
				STATE=MQTT_SRVTIME;
				}

			if(trigger_timer("PROCESS_EXPIRE",120000)){

				STATE_CALL=MQTT_LISTEN;
				STATE=MSG_EXPIRETIME;
				}

			if( (millis()-t_stats)/1000 > MQTT_STATS){

				t_stats = millis();

				STATE_CALL=MQTT_LISTEN;
				STATE=UPDATE_STATS;
				}

			if( (millis()-t_check)/1000 > MQTT_CHECK_TIME || event_interrupt){

				if( (millis()-t_check)/1000 > MQTT_CHECK_TIME)log(TRACE,"Time Trigger Start Process Message Queues");
				t_check = millis();

				STATE_CALL=MQTT_LISTEN;
				STATE=MQTT_PROCESS;
				}

			break;

		case MQTT_SRVTIME:
			{
			if(hasSPIFFS){

				uptime_ms=0;

				//read process server time
				fs::File ftime = SPIFFS.open("/etc/process.time", "r");

				if (ftime){

					if(ftime.available()) {

					  String rdline = ftime.readStringUntil('\n');
					  uptime_ms = atol(rdline.c_str());
					  }

					ftime.close();
					}

				{
				char temp[32];
				sprintf(temp,"Server UpTime %ld add %ld ms",uptime_ms,(millis()-inc_uptime));
				log(TRACE,temp);
				}

				uptime_ms += (millis()-inc_uptime);
				inc_uptime=millis();

				ftime = SPIFFS.open("/etc/process.time", "w");

				ftime.println(uptime_ms);

				ftime.close();
				}

			STATE=STATE_CALL;
			}
			break;

		case MQTT_EVENT:

			{
			//OK RECEIVED
			{
            char temp[32];
            sprintf(temp,"Session=%02d, Received %d bytes ",c, idx);
            log(TRACE,temp);
			}

			boolean setState=false;
			if(idx>0){

				int control_packet_type=IN_PACKET[0]>>4;

				//rework
				control_packet_type = control_packet_type << 4;

				{
				char temp[16];
				sprintf(temp,"> Control Packet = %d",control_packet_type);
				log(TRACE,temp);
				}

				setState=true;
				switch(control_packet_type){

					case MQTTCONNECT:
						STATE=MQTT_CONNECT;
						break;

					case MQTTSUBSCRIBE:
						STATE=MQTT_SUBSCRIBE;
						break;

					case MQTTPINGREQ:
						STATE=MQTT_PINGREQ;
						break;

					case MQTTPUBLISH:
						STATE=MQTT_PUBLISH;
						break;

					default:
						setState=false;
						break;
					}

				}

			if(!setState)STATE=STATE_CALL;
			}
			break;

		case MSG_EXPIRETIME:
			{
			log(TRACE,"Scan Messages to delete expired time Messages");

			long t=millis();
			wdt_disable();

			fs::Dir dir_data = SPIFFS.openDir("/topic");
			while (dir_data.next()) {

				String fname = dir_data.fileName();



				if(fname.startsWith("/topic/M") && fname.endsWith(".st")){

					fs::File msg_filest = SPIFFS.open(fname, "r");

					if(msg_filest.available()) {

						String rdline = msg_filest.readStringUntil('\n');

						msg_filest.close();

						String csv[3];
						int counter = 0;
						int lastIndex = 0;

						String processline = rdline+":";

						for (int i=0; i<processline.length(); i++) {

							if (processline.substring(i, i+1) == ":") {

								if(counter<3)csv[counter] = processline.substring(lastIndex, i);

								lastIndex = i + 1;
								counter++;
								}
							}

						if(counter>=3){

							long msgtime = atol(csv[2].c_str());
							if(msgtime>=0){

								msgtime = uptime_ms - msgtime;

								if( msgtime > (MAX_EXPIRE_MSGTIME * 60 * 1000)){

									log(TRACE,"Message "+fname+" is expired ");

									String msg_filename = fname.substring(0, fname.length()-3);

									fs::Dir dir_cd = SPIFFS.openDir(msg_filename);

									while (dir_cd.next()) {

										String cdname = dir_cd.fileName();
										if(cdname.endsWith(".cd")){

											log(TRACE,"Delete "+cdname+" ....");
											SPIFFS.remove(cdname);
											}
										}


									log(TRACE,"Remove msg,st and folder file "+msg_filename);

									SPIFFS.remove(msg_filename+".msg");
									SPIFFS.remove(msg_filename+".st");
									SPIFFS.remove(msg_filename);

								}else{

									log(TRACE,"Message "+fname+" has "+msgtime+" ms");
									}

							}else{

								log(TRACE,"Message "+fname+" is create as never expired");
								}
							}
						}

					}
				}

			t = millis()-t;
            {
			char temp[32];
			sprintf(temp,"Process Expire Message Time %ld ms", t);
			log(TRACE,temp);
			}

			wdt_enable(20000);
			}

			STATE=STATE_CALL;
			break;

		case MQTT_PROCESS:

			{
			log(TRACE,"Process Message Queues");
			long t;

			//flag done
			event_interrupt=false;

			wdt_disable();

			//SLOT
			int processed=0;
			int current_process=0;
			int skip = offset_process;
			boolean stop_max_process=false;

			fs::Dir dir_data = SPIFFS.openDir("/topic");
			while (dir_data.next() && !stop_max_process) {

			  //
			  //fs::File f = dir_data.openFile("r");

			  String fname = dir_data.fileName();

			  boolean GO=false;
			  if(fname.startsWith("/topic/M") && fname.endsWith(".st")){

				if(current_process<skip){

					//skip
					current_process++;
				}else{

					if(processed<MAX_PROCESS){

						processed++;
						GO=true;

					}else{
						offset_process+=MAX_PROCESS;
						if(processed==0)offset_process=0;
						stop_max_process=true;
						}
					}
				}


			  //if(fname.startsWith("/topic/M") && fname.endsWith(".st")){
			  if(GO){

				t=millis();

				String CD="";

				log(TRACE,"Open and process "+fname+" ("+processed+")("+skip+")");
				fs::File msg_filest = SPIFFS.open(fname, "r");

				//read all delivery list
				boolean skipfirst=true;
				String HeaderLine="";

				while(msg_filest.available()) {

					  String rdline = msg_filest.readStringUntil('\n');

					  char buf[rdline.length()];
					  sprintf(buf,"%s",rdline.c_str());
					  buf[rdline.length()-1]='\0';
					  rdline=String(buf);

					  if(skipfirst){

						HeaderLine=rdline;
						skipfirst=false;

						log(TRACE,">Headerline = "+HeaderLine);

						}else{

							//delivery list
							log(TRACE,">"+rdline);

							//process st
							{
							log(TRACE,"Parse line");
							String csv[2];
							int counter = 0;
							int lastIndex = 0;

							String processline = rdline+":";

							for (int i=0; i<processline.length(); i++) {

								if (processline.substring(i, i+1) == ":") {

									if(counter<2)csv[counter] = processline.substring(lastIndex, i);

									lastIndex = i + 1;
									counter++;
									}
								}

							if(counter>=2){

								//check connected Client ID & CD file
								log(TRACE,"Check "+csv[0]+" and file "+csv[1]);

								//check if clientid is connected
								log(TRACE,"Check if ClientID="+csv[0]+" is connected and in-session");

								//first, check for internal server subscription
								log(TRACE,"> check if server internal subs");
								if(mqtt_callback && strcmp(SERVER_INSTANCE_NAME.c_str(), csv[0].c_str() )==0){

									if(!internal_callback_paused){

										log(TRACE,"> server subs");
										log(TRACE,"Check if cd file "+csv[1]+" is still pending");
										if(SPIFFS.exists(csv[1])){

											log(TRACE,">CD file exists");
											CD+=csv[1]+":";
											}
									}else{

										log(TRACE,"> Internal callback is paused");
										}

								}else{

									for(int i=0;i < MAX_SRV_CLIENTS; i++){

										if( mqtt_clients[i].connected() && SERVER_SESSION[i].State==SESSION_ALLOCATED && strcmp(csv[0].c_str(), SERVER_SESSION[i].ClientID.c_str())==0){

											log(TRACE,">Client ID is in session");

											log(TRACE,"Check if cd file "+csv[1]+" is still pending");
											if(SPIFFS.exists(csv[1])){

												log(TRACE,">CD file exists");
												CD+=csv[1]+":";
												}
											}
										}
									}
								//----
								}
							}

						}

					}

				msg_filest.close();

				t = millis()-t;
                {
				  char temp[32];
				  sprintf(temp,"Index Message Time %ld ms", t);
				  log(TRACE,temp);
				}
				t=millis();

				//Esito
				boolean remove_state_file = false;
				String msg_filename = fname.substring(0, fname.length()-3);

				if(CD!=""){

					log(TRACE,"Try to deliver (csv list) "+CD);

					int RC=0;

					log(TRACE,"Process "+fname);
					fs::File msg_filedat;

					//retrieve Payload
					log(TRACE,">get Message Payload");
					String PAYLOAD="";

					log(TRACE,">msg file name payload = "+msg_filename+".msg");

					msg_filedat = SPIFFS.open(msg_filename+".msg", "r");
					if (!msg_filedat) {

					  log(TRACE,">Problem reading msg file data");
					  remove_state_file=true;
					  RC=-1;

					}else{

					  while(msg_filedat.available()) {

						String rdline = msg_filedat.readStringUntil('\n');
						if(rdline.length()>0)PAYLOAD+=rdline;
						}

					  }

					msg_filedat.close();

					//LOG
					if(PAYLOAD.length()>32){

					  log(TRACE,">Payload = >"+PAYLOAD.substring(0,32)+" ....");
					}else{
					  log(TRACE,">Payload = >"+PAYLOAD+"<");
					  }


					/*
					log(TRACE,"Open Client Destination "+msg_filename);
					fs::Dir dir_cd = SPIFFS.openDir(msg_filename);

					while (dir_cd.next()) {

					  fs::File fcd = dir_cd.openFile("r");

					  String cdname = dir_cd.fileName();

					  */

					//update st file
					/*
					log(TRACE,"Update Status "+fname+" file");
					msg_filest = SPIFFS.open(fname, "w");
					if (!msg_filest) {

						log(TRACE,">Problem creating st file data");
					}else{

						//clientid-msgfileid-msgtime-
						msg_filest.println(HeaderLine);
						}

					msg_filest.close();
					*/
					int lastIndex1 = 0;

					//verifica se rallentare
					boolean slowDown=false;
					if(slowDownInternalCallback){

						//rallenta a meno che non scatti il trigger
						slowDown=true;

						//2 secs
						if(millis() - t_slowDownInternalCallback > 2000){

							t_slowDownInternalCallback=millis();
							slowDown=false;

							log(TRACE,"Slow down condition bypassed");
							}
						}

					for (int i=0; i<CD.length(); i++) {

						//28.07
						ESP.wdtFeed();

						String cdname="";
						if (CD.substring(i, i+1) == ":") {

							cdname = CD.substring(lastIndex1, i);

							lastIndex1 = i + 1;
							}

					   if(cdname.endsWith(".cd")){

						boolean remove_cd_file = false;

						log(TRACE,"Open Client Destination File "+cdname);
						fs::File msg_filecd = SPIFFS.open(cdname, "r");

						//read all delivery list
						while(RC==0 && msg_filecd.available()) {

						  String rdline = msg_filecd.readStringUntil('\n');
						  char buf[rdline.length()];
						  sprintf(buf,"%s",rdline.c_str());
						  buf[rdline.length()-1]='\0';
						  rdline=String(buf);

						  //delivery list
						  log(TRACE,">"+rdline);

						  //process msg
						  {
						  log(TRACE,"Parse line");
						  String csv[16];
						  int counter = 0;
						  int lastIndex = 0;

						  String processline = rdline+":";

						  for (int i=0; i<processline.length(); i++) {

							if (processline.substring(i, i+1) == ":") {

							  if(counter<16)csv[counter] = processline.substring(lastIndex, i);

								lastIndex = i + 1;
								counter++;
								}
							 }

						  if(counter>=4){

							 //check if there is a subscription
							 log(TRACE,"Check subscription "+csv[2]);
							 if(!SPIFFS.exists(csv[2])){

							  log(TRACE,"> not exists");
							  remove_cd_file=true;
							}else{

							 //check if clientid is connected
							 log(TRACE,"Check if ClientID="+csv[0]+" is connected and in-session");

							 //first check server subs
							 log(TRACE,"> check if server internal subs");
							 if( mqtt_callback && strcmp(SERVER_INSTANCE_NAME.c_str(), csv[0].c_str() )==0){

								log(TRACE,"> server subs, deliver message");
								if(!slowDown){
									String TOPIC=csv[3];

									unsigned int payload_len = PAYLOAD.length();
									uint8_t payload[payload_len];

									PAYLOAD.getBytes( payload, payload_len);

									char topic[64];
									sprintf(topic,"%s",TOPIC.c_str());

									//se callback return false, then create cd file
									if(mqtt_callback( topic, payload, payload_len)){

										//STATS
										out_msg++;

										log(TRACE,"> Callback ok, set Remove cd file");
										remove_cd_file=true;

										//ripristina lo scodamento interno
										slowDownInternalCallback=false;
										c_slowDownInternalCallback=0;

									}else{

										c_slowDownInternalCallback++;
										if(c_slowDownInternalCallback>256){

											//callback dosen't accept message destination, rallenta lo scodamento interno
											slowDownInternalCallback=true;
											t_slowDownInternalCallback=millis();
											}
										}

								}else{

									log(TRACE,"Slow down condition");
									}

							 }else{

								 log(TRACE,"loop server clients");
								 for(int i=0;i < MAX_SRV_CLIENTS; i++){
									/*
								   {
								   char temp[16];
								   sprintf(temp,">loop=%d",i);
								   log(TRACE,temp);
								   }*/

								   //28.07
								   ESP.wdtFeed();

								   //in teoria non serve, bloccato in fase di connect
								   boolean acl_block=false;

								   String admin_topic = "/SYS/"+MqTT_Server_InstanceID+"/console";
								   if( mqtt_clients[i].connected() &&
										SERVER_SESSION[i].State==SESSION_ALLOCATED &&
											csv[3]==admin_topic && !SERVER_SESSION[i].isAdminSession ){

										log(TRACE,"Client ID "+SERVER_SESSION[i].ClientID+" is not in admin session");
										acl_block=true;
										}

								   if( mqtt_clients[i].connected() &&
										SERVER_SESSION[i].State==SESSION_ALLOCATED &&
											strcmp(csv[0].c_str(), SERVER_SESSION[i].ClientID.c_str())==0 && !acl_block){

									 log(TRACE,">Client ID is in session");

									 if(strcmp(csv[1].c_str(), "0") ==0){

									   log(TRACE,"Deliver QoS 0 message");

									   String TOPIC=csv[3];

									   //create RESPONSE
									   log(TRACE,"Create Response Buffer");
									   //2017.06.27
									   ESP.wdtFeed();

									   log(TRACE,">Send to "+TOPIC);
									   //String PAYLOAD = "ABC";
									   int len = 2+TOPIC.length()+PAYLOAD.length();

									   {
									   char temp[16];
									   sprintf(temp,">len=%d ",len);
									   log(TRACE,temp);
									   }

									   //work for remaining length
									   uint8_t REMANING_LEN[2];
									   uint8_t llen = 0;
									   uint8_t digit;
									   unsigned int pos = 0;

									   do {
										   digit = len % 128;
										   len = len / 128;
										   if (len > 0) {
											  digit |= 0x80;
											}
										   if(pos<2)REMANING_LEN[pos++] = digit;
											 llen++;
									   } while(len>0);

									   //calculate LEN -> header + RemainLength + Topic + 2 + 2 + Payload
									   len = 1+pos+TOPIC.length()+2+PAYLOAD.length();
									   {
									   char temp[16];
									   sprintf(temp,">packet len=%d, pos=%d ",len,pos);
									   log(TRACE,temp);
									   }

									   uint8_t OUT_PACKET[len];

									   //HEADER
									   unsigned int p=0;
									   OUT_PACKET[p++]=0x30;

									   //REMAIN LENGTH
									   for(int y=0;y<pos;y++){

										 OUT_PACKET[p++]=REMANING_LEN[0];
										 }

									   //TOPIC LENGTH
									   OUT_PACKET[p++]=TOPIC.length() >> 8;
									   OUT_PACKET[p++]=TOPIC.length() & 0xFF;

									   //copy topic to PACKET
									   for(int y=0;y<TOPIC.length();y++){

										 OUT_PACKET[p++]=TOPIC[y];
										 }

									   //COPY PAYLOAD
									   for(int y=0;y<PAYLOAD.length();y++){

										 OUT_PACKET[p++]=PAYLOAD[y];
										 }

									  //SEND DATA
									  log(TRACE,"Send packed data");
									  if(sendData(i, OUT_PACKET,len)){

										  //STATS
										  out_msg++;

										  //consegnato
										  remove_cd_file=true;

										  log(TRACE,"> done");
										  }
									  }

								   }else{

									 //non consegnato
									 }

								  }


								}
							}

						  }else{

								log(TRACE,"Wrong message state");
								remove_cd_file=true;
								}
							  }

						  //----
						  }

						msg_filecd.close();

						if(remove_cd_file){

						  log(TRACE,"Remove Client Destination File "+cdname);
						  SPIFFS.remove(cdname);

						}/*else{

						  //update st file
						  log(TRACE,"Update Status "+fname+" file");
						  msg_filest = SPIFFS.open(fname, "a+");
						  if (!msg_filest) {

							log(TRACE,">Problem updating st file data");
						  }else{

							msg_filest.println(cdname);
							}

						  msg_filest.close();

						  }*/
						}
					  }

					t = millis()-t;
					{
					  char temp[32];
					  sprintf(temp,"Delivery Message Time %ld ms", t);
					  log(TRACE,temp);
					}

					}//if CD!=""

				log(TRACE,"Check if there is CD files @ "+msg_filename);

				boolean found=false;
				fs::Dir dir_cd = SPIFFS.openDir(msg_filename);

				while (dir_cd.next() && !found) {

					  //fs::File fcd = dir_cd.openFile("r");

					  String cdname = dir_cd.fileName();
					  if(cdname.endsWith(".cd")){

						log(TRACE,"Found "+cdname+" ....");
						found=true;
						}
					  }

				if(!found){

					//Non ci sono CD
					remove_state_file=true;
					}

				if(remove_state_file){

					  log(TRACE,"Remove msg,st and folder file "+msg_filename);

					  SPIFFS.remove(msg_filename+".msg");
					  SPIFFS.remove(msg_filename+".st");
					  SPIFFS.remove(msg_filename);
					  }

				t=millis();
				}

			  }

			//skip
			if(!stop_max_process && processed<MAX_PROCESS)offset_process=0;

			wdt_enable(20000);

			t_check = millis();
			}

			STATE=STATE_CALL;
			break;

		case MQTT_CONNECT:

			{

			{
			char temp[32];
			sprintf(temp,"Connect Request Control Packet Received [%02d]",c);
			log(TRACE,temp);
			}

			event_interrupt=false;

            boolean username_flag=false;
            boolean password_flag=false;
			String UserName="";
			String Password="";
            String ClientID="";
            boolean willretain_flag=false;
            int willqos_flag=0x00;
            boolean will_flag=false;
            boolean cleansession_flag=false;
            int protocol_version=4;

            if(IN_PACKET[2]==0 && IN_PACKET[3]==4 && IN_PACKET[4]==int('M') && IN_PACKET[5]==int('Q') && IN_PACKET[6]==int('T') && IN_PACKET[7]==int('T') && IN_PACKET[8]==4){
			//if(IN_PACKET[2]==0 && IN_PACKET[4]==int('M') && IN_PACKET[5]==int('Q') && IN_PACKET[6]==int('T') && IN_PACKET[7]==int('T') ){

				log(TRACE,"Receive MqTT 3.1.1 connection request");

				//prepare RESPONSE
                uint8_t RC=0x00;
                uint8_t ACK_CONNECT_FLAG = 0x00;

                //check packet flags
                if( (IN_PACKET[9] & 0x02) !=0 )cleansession_flag=true;
                if( (IN_PACKET[9] & 0x04) !=0 )will_flag=true;
				if( (IN_PACKET[9] & 0x80) !=0 )username_flag=true;
				if( (IN_PACKET[9] & 0x40) !=0 )password_flag=true;

                long keep_alive_time = IN_PACKET[10]<<8;
                keep_alive_time+=IN_PACKET[11];

                {
                  char temp[32];
                  sprintf(temp,">KeepAliveTime = %ld ",keep_alive_time);
                  log(TRACE,temp);
                }

				int POS=12;

                //get client ID [12.13]
                int ClientID_len = IN_PACKET[POS]<<8;
                ClientID_len+=IN_PACKET[POS+1];
                {
                  char temp[32];
                  sprintf(temp,"Client ID length = %d ",ClientID_len);
                  log(TRACE,temp);
                }

                for(int i=0;i < ClientID_len; i++){

                  ClientID+=char(IN_PACKET[POS+2+i]);
                  }

                log(TRACE,">ClientID = "+ClientID);
                POS+=(2+ClientID_len);

				//get will topic / will message
				if(will_flag){

					log(TRACE,">Will flag");
					int WillTopic_len = 0;
					int WillMessage_len = 0;

					WillTopic_len = IN_PACKET[POS]<<8;
					WillTopic_len+=IN_PACKET[POS+1];
					{
					  char temp[32];
					  sprintf(temp,"WillTopic length = %d ",WillTopic_len);
					  log(TRACE,temp);
					}
					POS+=(2+WillTopic_len);

					WillMessage_len = IN_PACKET[POS]<<8;
					WillMessage_len+=IN_PACKET[POS+1];
					{
					  char temp[32];
					  sprintf(temp,"WillMessage length = %d ",WillMessage_len);
					  log(TRACE,temp);
					}
					POS+=(2+WillMessage_len);

					}

				//2017.07.17
				//check if user & password
				if(username_flag){
					log(TRACE,">Username flag");

					int UserName_len = IN_PACKET[POS]<<8;
					UserName_len+=IN_PACKET[POS+1];
					{
					  char temp[32];
					  sprintf(temp,"UserName length = %d ",UserName_len);
					  log(TRACE,temp);
					}

					for(int i=0;i < UserName_len; i++){

					  UserName+=char(IN_PACKET[POS+2+i]);
					  }

					log(TRACE,">UserName = "+UserName);
					POS+=(2+UserName_len);
					}

				if(password_flag){
					log(TRACE,">Password flag");

					int Password_len = IN_PACKET[POS]<<8;
					Password_len+=IN_PACKET[POS+1];
					{
					  char temp[32];
					  sprintf(temp,"Password length = %d ",Password_len);
					  log(TRACE,temp);
					}

					for(int i=0;i < Password_len; i++){

					  Password+=char(IN_PACKET[POS+2+i]);
					  }

					if(Password_len>1)log(TRACE,">Password = "+Password.substring(0,1)+"*****");

					POS+=(2+Password_len);
					}

				if(USER_AUTH){

					log(TRACE,"Server has user authorization");

					//2017.07.17
					//first check auth
					if(!username_flag && password_flag)RC=0x04;
					if(!connectAuth(UserName, Password)){

						log(TRACE,"User Authorization failed");
						RC=0x05;
						}

					/*
					if(isAdminSession(UserName, Password)){

						log(TRACE,"Admin Session, force clean session=true");
						cleansession_flag=true;
						}
					*/

				}else{
					/*
					if(username_flag && password_flag && isAdminSession(UserName, Password)){

						log(TRACE,"Admin Session, force clean session=true");
						cleansession_flag=true;
						}
					*/
					}

				if(RC==0x00){

					//flags print
					if(cleansession_flag){

						log(TRACE,">Clean Session Flag = 1");
						CleanClientSession(ClientID);
						}

					//disconnect other same ClientID
					for(int i=0;i < MAX_SRV_CLIENTS; i++){

					  //i!=c escludi la current connection
					  if( i!=c && SERVER_SESSION[i].State==SESSION_ALLOCATED && strcmp(ClientID.c_str(), SERVER_SESSION[i].ClientID.c_str())==0){

						//SP Flag
						if(!cleansession_flag){

						  log(TRACE,"Set SP flag");
						  ACK_CONNECT_FLAG=ACK_CONNECT_FLAG||0x01;
						  }

						{
						  char temp[64];
						  sprintf(temp,"Found same Client ID @ %d, force disconnect",i);
						  log(TRACE,temp);
						}

						freeSession(i);
						if (mqtt_clients[i]){

						   log(TRACE,"Send disconnect MqTT packet");

						  //SEND Disconnect Packet
						  uint8_t DISCONNECT_PACKET[2] = {0xE0, 0x00};
						  sendData(i, DISCONNECT_PACKET,2);

						  mqtt_clients[i].flush();
						  mqtt_clients[i].stop();
						  }

						log(TRACE,"Disconnected");
						}
					  }

					//Alloc SESSION
					if(!allocSession(c, ClientID,keep_alive_time,cleansession_flag,UserName,Password)){

					  log(TRACE,"Connection Refused RC=3");
					  RC=0x03;
					}else{

					  event_interrupt=true;
					  }
					}
                //create RESPONSE
                uint8_t OUT_PACKET[4] = {0x20, 0x02, ACK_CONNECT_FLAG, RC};
                sendData(c, OUT_PACKET,4);
                }
            }
			STATE=MQTT_LISTEN;
			break;

		case MQTT_PINGREQ:

			{
			log(TRACE,"Ping Request Control Packet Received");

            {
            char temp[64];
            long AliveTime = millis()-SERVER_SESSION[c].AliveTime;

            sprintf(temp,"Time since last PING %ld ms",AliveTime);
            log(TRACE,temp);
            }

            SERVER_SESSION[c].AliveTime=millis();
            event_interrupt=true;

            //create RESPONSE
            uint8_t OUT_PACKET[2] = {0xD0, 0x00};
            sendData(c, OUT_PACKET,2);
			}
			STATE=MQTT_LISTEN;
			break;

		case MQTT_SUBSCRIBE:

			{
			log(TRACE,"SubScribe Request Control Packet Received");

            String ClientID=SERVER_SESSION[c].ClientID;
            log(TRACE,">ClientID= "+ClientID);

			if(SERVER_SESSION[c].isAdminSession){

				log(TRACE,">Request from admin session");
				}

            int pos=0;
            int pid=0;
            int topic_filter_len=0;
            String topic_filter="";
            uint8_t qos=0;

            for(int x=0;x<idx;x++){

              switch(pos){

                case -1:
                  //something goes wrong, stay wrong
                  break;

                case 0:
                  {
                  int _control_packet_type=IN_PACKET[0]>>4;

                  //rework
                  _control_packet_type = _control_packet_type << 4;

                  if(_control_packet_type == MQTTSUBSCRIBE){

                    log(TRACE,"Receive SUB Request");
                    pos=1;
                    pid=0;
                    topic_filter_len=0;
                    topic_filter="";
                    qos=0;
                    }

                  }
                  break;

                case 1:
                  pos++;
                  break;

                case 2:
                  //packet id
                  pid = IN_PACKET[x]<<8;
                  pos++;
                  break;

                case 3:
                  //packet id
                  pid+=IN_PACKET[x];
                  pos++;

                  {
                  char temp[32];
                  sprintf(temp,">Packet ID=%d",pid);
                  log(TRACE,temp);
                  }
                  break;

                case 4:
                  //topic filter len
                  topic_filter_len = IN_PACKET[x]<<8;
                  pos++;
                  break;

                case 5:
                  //topic filter len
                  topic_filter_len+=IN_PACKET[x];
                  pos++;

                  {
                  char temp[32];
                  sprintf(temp,">Topic Filter Len=%d",topic_filter_len);
                  log(TRACE,temp);
                  }

                  break;

                default:

                  if(topic_filter.length()>=topic_filter_len){

                    //ok topic filter ended, get QoS
                    log(TRACE,">Topic Filter="+topic_filter);

                    qos = IN_PACKET[x];

                    {
                    char temp[32];
                    sprintf(temp,">QoS=%d",qos);
                    log(TRACE,temp);
                    }

                    //make subscription and respond ACK
                    pos=0;

                    int RC=0;
                    log(TRACE,"Generate SubScription");
                    {


                    if(topic_filter_len>0){

						if(RC==0 && (SERVER_SESSION[c].isAdminSession || USER_AUTH)){

							log(TRACE,"Check ACL list");
							log(TRACE,"> current user = "+SERVER_SESSION[c].UserName);

							String admin_topic = "/SYS/"+MqTT_Server_InstanceID+"/console";

							if(SERVER_SESSION[c].isAdminSession){

								log(TRACE,"> admin session");
								log(TRACE,"> admin topic="+admin_topic);

								if(strcmp(topic_filter.c_str(),admin_topic.c_str())!=0)RC=-99;

							}else{

								if(strcmp(topic_filter.c_str(),admin_topic.c_str())==0){

									log(TRACE,">Subscribe to admin topic without auth");
									RC=-98;
								}else{

									if(USER_AUTH){

										log(TRACE,"> loop acl file");
										}
									}
								}
							}

						if(RC==0){

							wdt_disable();

							String target_sub_finename="";
							{
							char temp[32];
							sprintf(temp, "/subs/CS%05d_%ld.sub" , subsid_inc, millis());
							target_sub_finename = String(temp);
							}

							//find subs
							long t = millis();

							int checked=0;
							fs::Dir dir_data = SPIFFS.openDir("/subs");
							while (dir_data.next()) {

							  //
							  String TOPIC = "";
							  String CLIENTID = "";

							  fs::File f = dir_data.openFile("r");

							  String fname = dir_data.fileName();

							  if(fname.startsWith("/subs/CS")){

								log(TRACE,"Found "+fname);
								checked++;

								int line=0;
								boolean stop=false;
								while(f.available() && !stop) {

								  String rdline = f.readStringUntil('\n');
								  char buf[rdline.length()];
								  sprintf(buf,"%s",rdline.c_str());
								  buf[rdline.length()-1]='\0';
								  rdline=String(buf);

								  switch(line){

									case 0:
									  CLIENTID=String(rdline);
									  break;

									case 1:
									  TOPIC=String(rdline);
									  break;

									default:

									  stop=true;
									  break;
									}

								  line++;
								  }
								}

							  f.close();

							  log(TRACE,"["+CLIENTID+"]["+ClientID+"]-["+TOPIC+"]["+topic_filter+"]");
							  if( CLIENTID.equals(ClientID) && checkMatchTopic(topic_filter, TOPIC)){

								//remove old subscription
								log(TRACE,"Remove "+fname);
								if(SPIFFS.remove(fname)){

								  log(TRACE,">File removed");

								  if(SERVER_SESSION[c].CleanSession){

									log(TRACE,">Clean Session=true, new="+target_sub_finename);
									CleanClientSession(CLIENTID);

								  }else{

									log(TRACE,"Clean Session=false, preserve SUB file name");
									target_sub_finename = fname;
									}


								  }
								}
							  }

							{
							char temp[32];
							t = millis()-t;
							sprintf(temp,"> subs loop time = %ld ms",t);
							log(TRACE,temp);
							}

							t=millis();

							//Write subscription file
							{
							String tmp = "Sub target file is "+target_sub_finename;
							log(TRACE, tmp);

							fs::File topicsub_filedat = SPIFFS.open(target_sub_finename, "w");
							if (!topicsub_filedat) {
							  log(TRACE,">Problem creating subscription file data");
							}else{

							  log(TRACE,">File is created");
							  topicsub_filedat.println(ClientID);
							  topicsub_filedat.println(topic_filter);
							  topicsub_filedat.println(pid);
							  topicsub_filedat.println(qos);
							  }

							topicsub_filedat.close();
							}

							{
							char temp[32];
							t = millis()-t;
							sprintf(temp,"> subs create time = %ld ms",t);
							log(TRACE,temp);
							}

							wdt_enable(20000);
							}

                      }else{

                        RC=-1;
                        }
                      }

                      {
                      char temp[32];
                      sprintf(temp,"RC=%d",RC);
                      log(TRACE,temp);
                      }

					  {
                      //create RESPONSE
                      uint8_t OUT_PACKET[5] = {0x90, 0x03, 0x00, 0x00, 0x00};
                      //0x03 -> length header var (2 byte) + length payload ( 1 byte)

                      //set PID
                      OUT_PACKET[2]= (uint8_t)(pid >>  8);
                      OUT_PACKET[3]= (uint8_t)(pid >>  0);

                      //set QoS
                      OUT_PACKET[4]= qos;

                      switch(RC){

                        case 0:
                          break;

                        default:
                          OUT_PACKET[4] = 0x80;
                          break;
                        }

                      sendData(c, OUT_PACKET,5);
                      }

					  {
					  switch(RC){

                        case 0:
                          break;

                        default:

						  log(TRACE,"Force disconnect due permission");
						  //create RESPONSE
						 //SEND Disconnect Packet
						  uint8_t DISCONNECT_PACKET[2] = {0xE0, 0x00};
						  sendData(c, DISCONNECT_PACKET,2);

                          break;
                        }
					  }

                    }else{

                      //inc topic filter
                      topic_filter+=char(IN_PACKET[x]);
                      pos++;
                      }

                    break;
                  }
                }
			}
			STATE=MQTT_LISTEN;
			break;

		case MQTT_PUBLISH:

			{
            log(TRACE,"Publish message Control Packet Received");

            String ClientID=SERVER_SESSION[c].ClientID;
            log(TRACE,">ClientID= "+ClientID);

			int idx_in =0;
			//+2 considera il packet identifier finale
			while( (idx_in+2) < idx){

				uint8_t dup=0;
				uint8_t qos=0;
				uint8_t retain=0;

				if( (IN_PACKET[idx_in+0] & 0x08))dup=1;
				qos =  (IN_PACKET[idx_in+0] >> 1) & 0x03;
				if( (IN_PACKET[idx_in+0] & 0x01))retain=1;

				{
				char temp[32];
				sprintf(temp,">Flag d%d q%d r%d", dup, qos, retain);
				log(TRACE,temp);
				}

				int offset_huge_payload_len=0;
				if(IN_PACKET[idx_in+1] > 0x7f){

				  log(TRACE,"Payload + Header Len is > 128 byte");
				  offset_huge_payload_len=1;
				  }

				int topic_filter_len;
				String topic_filter="";

				topic_filter_len = IN_PACKET[idx_in+offset_huge_payload_len+2]<<8;
				topic_filter_len+=IN_PACKET[idx_in+offset_huge_payload_len+3];

				{
				char temp[32];
				sprintf(temp,"Topic Filter length = %d ",topic_filter_len);
				log(TRACE,temp);
				}

				for(int i=0;i < topic_filter_len; i++){

				  topic_filter+=char(IN_PACKET[idx_in+offset_huge_payload_len+4+i]);
				  }

				log(TRACE,">Topic Filter = "+topic_filter);

				//2017.07.17 User Auth + Admin session
				int RC=0;
				boolean admin_payload=false;

				String admin_topic = "/SYS/"+MqTT_Server_InstanceID+"/console";
				if(RC==0 && (SERVER_SESSION[c].isAdminSession || USER_AUTH)){

					log(TRACE,"Check ACL list");
					log(TRACE,"> current user = "+SERVER_SESSION[c].UserName);

					if(SERVER_SESSION[c].isAdminSession){

						log(TRACE,"> admin session");
						log(TRACE,"> admin topic="+admin_topic);

						if(strcmp(topic_filter.c_str(),admin_topic.c_str())==0){
							admin_payload=true;
						}else{
							RC=-99;
							}

					}else{

						if(strcmp(topic_filter.c_str(),admin_topic.c_str())==0){

							log(TRACE,">Publish to admin topic without auth");
							RC=-98;

						}else{

							if(USER_AUTH){

								log(TRACE,"> loop acl file");
								}
							}


						}
					}


				int offset_qos=0;
				if(qos>0)offset_qos=2;

				int payload_len = 0;
				if(offset_huge_payload_len>0){

				  int count = IN_PACKET[idx_in+2];
				  payload_len = (0x7f*count) + IN_PACKET[idx_in+1] - 0x7f - topic_filter_len - offset_qos -2;

				}else{
				  payload_len = IN_PACKET[idx_in+1] - topic_filter_len - offset_qos -2;
				  }

				String payload= "";

				if(RC==0){

					{
					char temp[32];
					sprintf(temp,"Payload length = %d ",payload_len);
					log(TRACE,temp);
					}

					for(int i=0;i<payload_len;i++){

						payload+=char(IN_PACKET[idx_in+offset_huge_payload_len+4+topic_filter_len+offset_qos+i]);
						}

					//LOG
					if(payload.length()>32){

						log(TRACE,">Payload = "+payload.substring(0,32)+" ....");
					}else{
						log(TRACE,">Payload = "+payload);
						}

					if(admin_payload){

						log(TRACE,"Process Admin Payload");
						int RC=-99;

						String csv[8];
						int counter = 0;
						int lastIndex = 0;

						String processline = payload+" ";

						for (int i=0; i<processline.length(); i++) {

							if (processline.substring(i, i+1) == " ") {

								if(counter<8)csv[counter] = processline.substring(lastIndex, i);

								lastIndex = i + 1;
								counter++;
								}
							}

						//parse command
						if(counter>0 && strcmp(csv[0].c_str(), "LISTUSERS")==0){

							RC=0;
							fs::File auth_file = SPIFFS.open("/auth/passwd", "r");

							boolean first=true;
							payload+=" [";
							//read all
							while(RC==0 && auth_file.available()) {

								String rdline = auth_file.readStringUntil('\n');
								{
								String csv[2];
								int counter = 0;
								int lastIndex = 0;

								String processline = rdline+":";

								for (int i=2; i<processline.length(); i++) {

									if (processline.substring(i, i+1) == ":") {

										if(counter<2)csv[counter] = processline.substring(lastIndex, i);

										lastIndex = i + 1;
										counter++;
										}
									}

								if(counter>0){
									if(!first)payload+=",";
									payload+=csv[0];
									first=false;
									}
								}
								}

							auth_file.close();

							payload+="]";
							}

						if(counter>0 && strcmp(csv[0].c_str(), "ADDUSER")==0){

							if(counter>2 && csv[1].length()>0 && csv[2].length()>0){

								log(TRACE,"Try to register user="+csv[1]+" with password="+csv[2].substring(0,1)+"*****");

								RC=0;
								fs::File auth_file = SPIFFS.open("/auth/passwd", "r");

								//read all
								while(RC==0 && auth_file.available()) {

									String rdline = auth_file.readStringUntil('\n');
									if(rdline.startsWith(csv[1]+":")){

										log(TRACE,"User already defined");
										RC=-102;
										}
									}

								auth_file.close();

								if(RC==0){

									log(TRACE,"Append user to passwd file");

									auth_file = SPIFFS.open("/auth/passwd", "a+");
									auth_file.println(csv[1]+":"+csv[2]);
									auth_file.close();

									//update
									updateConfigFiles();
									}

							}else{

								RC=-101;
								}

							}

						{
						char temp[128];
						sprintf(temp,"RC=%d",RC);
						payload+=" ["+String(temp)+"]";
						}

						}

					}

					//
					idx_in+=offset_huge_payload_len+4+topic_filter_len+offset_qos+payload_len;
					{
					char temp[16];
					sprintf(temp,">Process %d/%d",idx_in,idx);
					log(TRACE,temp);
					}

					if(RC==0){
						storeMessage(ClientID, topic_filter, payload, dup, qos, retain);
						}
				}

				}

			STATE=MQTT_LISTEN;
			break;

		default:
			break;
		}

	}

//SERVER
boolean ME::isAdminSession(String UserName, String Password){

	log(TRACE,"Check if admin session");

	if( strcmp(ADMIN_USER.c_str(),UserName.c_str())==0 && strcmp(ADMIN_PASSWORD.c_str(),Password.c_str())==0 ){
		log(TRACE,">this is an admin session");
		return true;
		}

	return false;
	}

boolean ME::connectAuth(String UserName, String Password){

	log(TRACE,"Check auth user");

	{
	log(TRACE,">check if admin");

	log(TRACE,">"+ADMIN_USER+"<>"+UserName+"-"+ADMIN_PASSWORD+"<>"+Password+"<");
	if( strcmp(ADMIN_USER.c_str(),UserName.c_str())==0 && strcmp(ADMIN_PASSWORD.c_str(),Password.c_str())==0 ){
		log(TRACE,">admin validated");
		return true;
		}

	}

	{
	log(TRACE,">loop auth file");

	int RC=-99;
	fs::File auth_file = SPIFFS.open("/auth/passwd", "r");

	//read all
	while(RC<0 && auth_file.available()) {

		String rdline = auth_file.readStringUntil('\n');

		char buf[rdline.length()];
		sprintf(buf,"%s",rdline.c_str());
		buf[rdline.length()-1]='\0';
		rdline=String(buf);

		//log(TRACE,">found "+rdline);
		String csv[2];
		int counter = 0;
		int lastIndex = 0;

		String processline = rdline+":";

		for (int i=2; i<processline.length(); i++) {

			if (processline.substring(i, i+1) == ":") {

				if(counter<2)csv[counter] = processline.substring(lastIndex, i);

				lastIndex = i + 1;
				counter++;
				}
			}

		//log(TRACE,">compare >"+UserName+"<>"+csv[0]+"<>"+Password+"<>"+csv[1]+"<");
		log(TRACE,">Check "+UserName+"-"+csv[0]);
		//if(counter>=1 && strcmp(UserName.c_str(),csv[0].c_str())==0 && strcmp(Password.c_str(),csv[1].c_str())==0 ){
		if(counter>1 && UserName==csv[0] && Password==csv[1]){

			log(TRACE,"User found and validated");
			RC=0;
			}
		}

	auth_file.close();

	if(RC==0)return true;
	}

	return false;
	}

void ME::stop_callback(){
	internal_callback_paused=true;
	}

void ME::play_callback(){
	internal_callback_paused=false;
	}

void ME::CleanClientSession(String CLIENTID){

//Remove all cd files of ClientID
log(TRACE,"Remove cd files of "+CLIENTID);
fs::Dir dir_data = SPIFFS.openDir("/topic");
while (dir_data.next()) {

	boolean remove_cd_file=false;
	fs::File f = dir_data.openFile("r");

	String fname = dir_data.fileName();
	//log(TRACE,"Found "+fname);

	if(fname.endsWith(".cd")){

		boolean stop=false;
		while(f.available() && !stop) {

		  String rdline = f.readStringUntil('\n');
		  char buf[rdline.length()];
		  sprintf(buf,"%s",rdline.c_str());
		  buf[rdline.length()-1]='\0';
		  rdline=String(buf);

		  String csv[2];
		  int counter = 0;
		  int lastIndex = 0;

		  String processline = rdline+":";

		  for (int i=0; i<processline.length(); i++) {

			if (processline.substring(i, i+1) == ":") {

			  if(counter<2)csv[counter] = processline.substring(lastIndex, i);

				lastIndex = i + 1;
				counter++;
				}
			 }

		  if(counter>0 && strcmp(csv[0].c_str(), CLIENTID.c_str())==0){

			remove_cd_file=true;
			}

		  stop=true;
		  }
		}

	f.close();

	if(remove_cd_file){

		log(TRACE,"> remove "+fname);
		if(SPIFFS.remove(fname))log(TRACE,"> removed");
		}
	}
}

boolean ME::storeMessage(String ClientID, String topic_filter, String payload, uint8_t dup, uint8_t qos, uint8_t retain){

	//Set interrupt
	event_interrupt=true;

	//Store Message & create delivery info
	log(TRACE,"Store msg state");
	String msg_filename = "";

	{
	char temp[32];
	sprintf(temp, "/topic/M%05d_%ld" , msgid_inc, millis());
	msg_filename = String(temp);

	String tmp = "MSG file is "+msg_filename;
	log(TRACE, tmp);

	log(TRACE,"Store State");
	fs::File msg_filedat = SPIFFS.open(msg_filename+".st", "w");
	if (!msg_filedat) {

	  log(TRACE,">Problem creating msg file data");
	}else{

	  //clientid-msgfileid-msgtime-
	  msg_filedat.println(ClientID+":"+msg_filename+":"+uptime_ms);
	  }

	msg_filedat.close();

	log(TRACE,"Store PAYLOAD");
	msg_filedat = SPIFFS.open(msg_filename+".msg", "w");
	if (!msg_filedat) {

	  log(TRACE,">Problem creating msg file data");
	}else{

	  msg_filedat.println(payload);
	  }

	msg_filedat.close();
	}

	//STATS
	in_msg++;

	//user for message state
	int inc_msg_state=0;

	switch(qos){

	  case 0:
		{
		log(TRACE,"Process message QoS=0");
		wdt_disable();

		long t=millis();
		fs::Dir dir_data = SPIFFS.openDir("/subs");
		while (dir_data.next()) {

		  fs::File f =  dir_data.openFile("r");

		  String fname = dir_data.fileName();

		  if(fname.startsWith("/subs/CS")){

			log(TRACE, ">"+fname);

			//
			String TOPIC = "";
			String CLIENTID = "";

			int line=0;
			boolean stop=false;
			while(f.available() && !stop) {

			  String rdline = f.readStringUntil('\n');
			  char buf[rdline.length()];
			  sprintf(buf,"%s",rdline.c_str());
			  buf[rdline.length()-1]='\0';
			  rdline=String(buf);

			  switch(line){

				case 0:
				  CLIENTID=String(rdline);
				  break;

				case 1:
				  TOPIC=String(rdline);
				  break;

				default:
				  stop=true;
				  break;
				  }

			  line++;
			  }

		  //Check if send data
		  if(line>1 && !CLIENTID.equals(ClientID)){

			//check topic
			boolean match = checkMatchTopic(topic_filter, TOPIC);

			if(match){

			  log(TRACE,"Found Subscription "+TOPIC+" for "+CLIENTID);

			  //default
			  String msg_state = "/0.cd";
			  {
			  char temp[8];
			  sprintf(temp,"/%d.cd",inc_msg_state);
			  msg_state=String(temp);
			  inc_msg_state++;
			  }

			   fs::File msg_filedat;

			  //Append ClientID to ST File also, used as index during process messages
			  log(TRACE,"Append Client ID="+CLIENTID+" to st file as index");
			  msg_filedat = SPIFFS.open(msg_filename+".st", "a+");
			  if (!msg_filedat) {

	            log(TRACE,">Problem appending st file "+msg_filename+".st");

			  }else{

				msg_filedat.println(CLIENTID+":"+msg_filename+msg_state);
				}

			  msg_filedat.close();

			  log(TRACE,"Append data to delivery list "+msg_filename+msg_state);

			  msg_filedat = SPIFFS.open(msg_filename+msg_state, "w");
			  if (!msg_filedat) {

				log(TRACE,">Problem append data to msg file data");
			  }else{

				//clientid-subscription file
				msg_filedat.println(CLIENTID+":"+String(qos)+":"+fname+":"+topic_filter);

				}

			  msg_filedat.close();
			  }
			}
		  }

		  f.close();
		  }

		t = millis()-t;
		{
		char temp[32];
		sprintf(temp,">Process QoS 0 Time %ld ms", t);
		log(TRACE,temp);
		}

		wdt_enable(20000);
		return true;
		}
		break;

	  case 1:
		{
		log(TRACE,"Process message QoS=1");
		wdt_disable();

		long t=millis();

		t = millis()-t;
		{
		char temp[32];
		sprintf(temp,"Process QoS 1 Time %d ms", t);
		log(TRACE,temp);
		}

		wdt_enable(20000);
		return true;
		}
		break;

	  default:
		break;
	  }

	return false;
	}

boolean ME::publish(const char* topic, const char* payload) {
    return publish(topic,(const uint8_t*)payload,strlen(payload),false);
	}

boolean ME::publish(const char* topic, const char* payload, boolean retained) {
    return publish(topic,(const uint8_t*)payload,strlen(payload),retained);
	}

boolean ME::publish(const char* topic, const uint8_t* payload, unsigned int plength) {
    return publish(topic, payload, plength, false);
	}

boolean ME::publish(const char* topic, const uint8_t* payload, unsigned int plength, boolean retained) {

	/*
	String _payload="";
	for (unsigned int i=0; i<plength; i++) {

		_payload += char(payload[i]);
		}*/

	return storeMessage(SERVER_INSTANCE_NAME, String(topic), String((char *)payload), 0, 0, retained);
	}

ME& ME::setCallback(MQTT_CALLBACK_SIGNATURE) {

	this->mqtt_callback = mqtt_callback;
    return *this;
	}

boolean ME::subscribe(String topic_filter, boolean CleanSession){

	log(TRACE,"Add Server Subscription");
	if(!mqtt_callback){

		log(TRACE,"> callback is not defined, add before");
		return false;
		}

	String target_sub_finename="";
    {
    char temp[32];
    sprintf(temp, "/subs/CS%05d_%ld.sub" , subsid_inc, millis());
    target_sub_finename = String(temp);
    }

    //find subs
    long t = millis();

    int checked=0;
    fs::Dir dir_data = SPIFFS.openDir("/subs");
    while (dir_data.next()) {

       //
       String TOPIC = "";
       String CLIENTID = "";

       fs::File f = dir_data.openFile("r");

       String fname = dir_data.fileName();

       if(fname.startsWith("/subs/CS")){

         log(TRACE,"Found "+fname);
         checked++;

         int line=0;
		boolean stop=false;
        while(f.available() && !stop) {

           String rdline = f.readStringUntil('\n');
           char buf[rdline.length()];
           sprintf(buf,"%s",rdline.c_str());
           buf[rdline.length()-1]='\0';
           rdline=String(buf);

           switch(line){

             case 0:
               CLIENTID=String(rdline);
               break;

             case 1:
               TOPIC=String(rdline);
               break;

             default:

			  stop=true;
               break;
             }

           line++;
           }
         }

       f.close();

       log(TRACE,"["+CLIENTID+"]["+SERVER_INSTANCE_NAME+"]-["+TOPIC+"]["+topic_filter+"]");

	   if( CLIENTID.equals(SERVER_INSTANCE_NAME) && checkMatchTopic(topic_filter, TOPIC)){

            //remove old subscription
            log(TRACE,"Remove "+fname);
            if(SPIFFS.remove(fname)){

              log(TRACE,">File removed");

              if(CleanSession){

                log(TRACE,">Clean Session=true, new="+target_sub_finename);
				CleanClientSession(SERVER_INSTANCE_NAME);

              }else{

                log(TRACE,"Clean Session=false, preserve SUB file name");
                target_sub_finename = fname;
                }


              }
            }
	   }

	{
    char temp[32];
    t = millis()-t;
    sprintf(temp,"> server subs loop time = %ld ms",t);
    log(TRACE,temp);
    }

    t=millis();

	//Write subscription file
    {
    String tmp = "Sub target file is "+target_sub_finename;
    log(TRACE, tmp);

    fs::File topicsub_filedat = SPIFFS.open(target_sub_finename, "w");
    if (!topicsub_filedat) {
      log(TRACE,">Problem creating subscription file data");
    }else{

      log(TRACE,">File is created");
      topicsub_filedat.println(SERVER_INSTANCE_NAME);
      topicsub_filedat.println(topic_filter);
      topicsub_filedat.println(0);
      topicsub_filedat.println(0);
      }

    topicsub_filedat.close();
    }

    {
    char temp[32];
    t = millis()-t;
    sprintf(temp,"> server subs create time = %ld ms",t);
    log(TRACE,temp);
    }

	return false;
	}

boolean ME::unsubscribe(String topic){

	log(TRACE,"Remove Server Subscription");
	/*
	boolean found=false;
	for(int i=0;i<MAX_LISTENER;i++){

		if( topic_listener[i]!="" && checkMatchTopic(topic, topic_listener[i]) ){

			topic_listener[i]="";
			found=true;
			}
		}

	return found;
	*/
	return false;
	}

boolean ME::checkMatchTopic(String topic1, String topic2){

  String csv[16];
  int counter = 0;
  int lastIndex = 0;

  String _topic_filter = topic1+"/";

  for (int i=0; i<_topic_filter.length(); i++) {

    if (_topic_filter.substring(i, i+1) == "/") {

      if(counter<16)csv[counter] = _topic_filter.substring(lastIndex, i);

        lastIndex = i + 1;
        counter++;
        }
    }

  _topic_filter = topic2+"/";
  counter=0;
  lastIndex = 0;

  boolean match=true;
  for (int i=0; i<_topic_filter.length() && match; i++) {

    if (_topic_filter.substring(i, i+1) == "/") {

      if(counter<16){
        String path = _topic_filter.substring(lastIndex, i);
        if(!path.equals("+") && !csv[counter].equals("+") && !path.equals(csv[counter]) )match=false;
        }

      lastIndex = i + 1;
      counter++;
      }
  }

  return match;
  }

boolean ME::sendData(int idx,uint8_t DATA[], int n){

  {
  char temp[32];
  sprintf(temp,"> %d bytes to %d",n,idx);
  log(TRACE,temp);
  }

  //int n = sizeof(DATA) / sizeof(uint8_t);
  for(int i=0;i<n;i++){

	//Serial.print(mqtt_clients[idx]);
	//Serial.print("-");
	//Serial.print(mqtt_clients[idx].connected());
	//Serial.print("-");
	//if(mqtt_clients[idx].connected()){

	//	Serial.println(mqtt_clients[idx].write(DATA[i]));
		//Serial.println(mqtt_clients[idx].available());
	//}else{

	//	Serial.print("PROBLEM");
	//	}
	if(mqtt_clients[idx].write(DATA[i])==0){

		log(TRACE,"Client Lost :-(");
		return false;
		}
    }

  //log
  {
    String temp = "Send[";
	for(int i=0;i<n;i++){
		char d[8];
		sprintf(d," %x",DATA[i]);
		temp+=String(d);
		}
	temp+=" ]";
	log(TRACE,temp);
  }

  return true;
  }

void ME::freeSession(int idx){

  SERVER_SESSION[idx].State=SESSION_FREE;
  SERVER_SESSION[idx].ClientID="";
  SERVER_SESSION[idx].KeepAliveTime=0;
  SERVER_SESSION[idx].CleanSession=false;
  }

boolean ME::allocSession(int c, String ClientID, long KeepAliveTime, boolean CleanSession, String UserName, String Password){

  if( SERVER_SESSION[c].State==SESSION_FREE ){

    SERVER_SESSION[c].State=SESSION_ALLOCATED;
    SERVER_SESSION[c].ClientID=ClientID;
    SERVER_SESSION[c].KeepAliveTime=KeepAliveTime;
    SERVER_SESSION[c].AliveTime=millis();
    SERVER_SESSION[c].CleanSession=CleanSession;
	SERVER_SESSION[c].UserName=UserName;
	SERVER_SESSION[c].Password=Password;

    SERVER_SESSION[c].isAdminSession = isAdminSession(UserName, Password);

    {
      char temp[32];
      sprintf(temp,"Client ID has Session @ %d",c);
      log(TRACE,temp);
    }

    return true;
    }

  return false;
  }

//EEPROM
void ME::save_field_EEPROM(int MEM_POS, String field, int field_len){

   //log(TRACE, "Save FIELD="+field);
   for (int i=MEM_POS,x=0; i<(field_len+MEM_POS); ++i,++x) {

        if(x < field.length()){

          //Serial.print("Wrote: ");
          //Serial.println(field[x]);
          EEPROM.write(i, field[x]);
        }else{

          EEPROM.write(i, ' ');
          }
     }

	EEPROM.commit();
	}

String ME::load_field_EEPROM(int MEM_POS, int field_len){

   String field="";

   for (int i=MEM_POS,x=0; i<(field_len+MEM_POS); ++i,++x) {

     field += char(EEPROM.read(i));
     }

   field.trim();
   return field;
	}

//CONFIG
void ME::updateConfigFiles(){

	log(TRACE,"Update config files for www");

	{
	log(TRACE,"> config.val");
	fs::File f = SPIFFS.open("/www/data/config.val", "w");
	f.println(IP1+"."+IP2+"."+IP3+"."+IP4);
	f.println(MQTT_PORT);
	f.println(AP_SSID);
	f.close();
	}

	{
	log(TRACE,"> users.val");
	fs::File f = SPIFFS.open("/www/data/users.val", "w");

	fs::File u = SPIFFS.open("/auth/passwd", "r");
	while(u.available()){

		String rdline = u.readStringUntil('\n');
		log(TRACE,rdline);

		f.println(rdline);
		}
	u.close();

	f.close();
	}

	{
	log(TRACE,"> parameter.val");
	fs::File f = SPIFFS.open("/www/data/parameters.val", "w");
	f.close();

	log(TRACE,"Update parameters list");


	fs::Dir dir_data = SPIFFS.openDir("/etc");
	while (dir_data.next()) {

		String paramline="";
		fs::File f = dir_data.openFile("r");

		String fname = dir_data.fileName();

		if(fname.startsWith("/etc/param")){

			log(TRACE,"> add "+fname);
			if(f.available()){

				paramline = f.readStringUntil('\n');
				}
			}
		f.close();

		if(paramline.length()>0){

			log(TRACE,"> append "+paramline);
			f = SPIFFS.open("/www/data/parameters.val", "a+");
			f.println(paramline);
			f.close();
			}
		}
	}

	}

int ME::getConfigStatus(){

    return CONFIG_STATUS;
    }

void ME::load_cfg(){

	log(TRACE,"Start Config Load");

	//-------------
    //0   1    	     17     49      65          81        93       97   98			106		   114	  115
	//CFG DEVICENAME SERIAL AP_SSID AP_PASSWORD IPADDRESS MQTTPORT CRC  ADMIN USER  ADMIN PSW  SECURE

	String CRC = load_field_EEPROM(97,1);
    String CFG = load_field_EEPROM(0,1);
    log(TRACE,"Module check config ["+CFG+"]["+CRC+"]");

	if(CRC=="9"){

		CONFIG_STATUS = atoi( CFG.c_str() );

		switch(CONFIG_STATUS){

			case 1:
				{
				log(TRACE,"Module is configured");

				CONFIG_STATUS=ME_CONFIGURED;

				AP_SSID = load_field_EEPROM(49,16);
				AP_PASSWORD = load_field_EEPROM(65,16);
				/*
				IP1 = load_field_EEPROM(81,3);
				IP2 = load_field_EEPROM(84,3);
				IP3 = load_field_EEPROM(87,3);
				IP4 = load_field_EEPROM(90,3);
				*/
				SERVER_INSTANCE_NAME = load_field_EEPROM(1,16);
				SERVER_SERIALNUMBER = load_field_EEPROM(17,32);
				MQTT_PORT = atoi( load_field_EEPROM(93,4).c_str() );

				log(TRACE,">SERVER NAME="+SERVER_INSTANCE_NAME+" @ "+load_field_EEPROM(93,4));
				log(TRACE,">AP Config Loaded SSID="+AP_SSID+" PASSWORD="+AP_PASSWORD);
				WiFi.persistent(false);
				WiFi.mode(WIFI_OFF);
				WiFi.mode(WIFI_AP);

				IPAddress ip(
				   atoi(IP1.c_str()),
				   atoi(IP2.c_str()),
				   atoi(IP3.c_str()),
				   atoi(IP4.c_str())
				   );


				IPAddress gateway(
				   atoi(GW_IP1.c_str()),
				   atoi(GW_IP2.c_str()),
				   atoi(GW_IP3.c_str()),
				   atoi(GW_IP4.c_str())
				   );

				IPAddress subnet(
				   atoi(SB_IP1.c_str()),
				   atoi(SB_IP2.c_str()),
				   atoi(SB_IP3.c_str()),
				   atoi(SB_IP4.c_str())
				   );

				ADMIN_USER = load_field_EEPROM(98,8);
				ADMIN_PASSWORD = load_field_EEPROM(106,8);
				//log(TRACE,"Set MqTT Management "+(ADMIN_USER.substring(0,1)) +"**** "+(ADMIN_PASSWORD.substring(0,1)) +"**** ");
				log(TRACE,"Set MqTT Management "+ADMIN_USER+" "+(ADMIN_PASSWORD.substring(0,1)) +"**** ");

				log(TRACE,">IP="+IP1+"."+IP2+"."+IP3+"."+IP4+" SUBNET="+SB_IP1+"."+SB_IP2+"."+SB_IP3+"."+SB_IP4+" GATEWAY="+GW_IP1+"."+GW_IP2+"."+GW_IP3+"."+GW_IP4);
				WiFi.softAPConfig(ip, gateway, subnet);
				WiFi.softAP(AP_SSID.c_str(), AP_PASSWORD.c_str());

				//2017.07.18
				if( load_field_EEPROM(114,1)=="1"){

					log(TRACE,">MqTT security enabled");
					USER_AUTH=true;
					}

				log(TRACE,">WiFi loaded");
				}
				break;

			case 9:
				uint8_t mac[WL_MAC_ADDR_LENGTH];
				WiFi.softAPmacAddress(mac);

				log(TRACE,"Turn ON AP="+ssid);
				WiFi.softAP(ssid.c_str(), password.c_str());
				break;

			default:
				break;
			}

	}else{

		log(TRACE,"Config corrupted, set as unconfigured");
		CONFIG_STATUS=ME_UNCONFIGURED;

		save_field_EEPROM(0,"9",1);
		save_field_EEPROM(97,"9",1);

		uint8_t mac[WL_MAC_ADDR_LENGTH];
		WiFi.softAPmacAddress(mac);

		log(TRACE,"Turn ON AP="+ssid);
		WiFi.softAP(ssid.c_str(), password.c_str());
		}

	log(TRACE,"End Config Load");
	}

//WWW
bool ME::is_authentified(){

  log(TRACE,"www Enter is_authentified");
  if (www_server.hasHeader("Cookie")){

	String cookie = www_server.header("Cookie");
	log(TRACE,"Found cookie: "+cookie);

    if (cookie.indexOf("ESPSESSIONID=1") != -1) {
      log(TRACE,"www Authentification Successful");
      return true;
	  }
	}

  log(TRACE,"www Authentification Failed");
  return false;
}

void ME::handle_login_internal(){

	char temp[1024];

	wdt_disable();
	log(TRACE,"Serve internal login");

	String msg;
	if (www_server.hasHeader("Cookie")){

		log(TRACE,"Found cookie: "+www_server.header("Cookie"));
		}

	if (www_server.hasArg("DISCONNECT")){

		log(TRACE,"www Disconnection");
		www_server.sendHeader("Location","/login");
		www_server.sendHeader("Cache-Control","no-cache");
		www_server.sendHeader("Set-Cookie","ESPSESSIONID=0");
		www_server.send(301);
		return;
		}

	if (www_server.hasArg("USERNAME") && www_server.hasArg("PASSWORD")){
		if (www_server.arg("USERNAME") == ADMIN_USER &&  www_server.arg("PASSWORD") == ADMIN_PASSWORD ){

			www_server.sendHeader("Location","/admin.htm");
			www_server.sendHeader("Cache-Control","no-cache");
			www_server.sendHeader("Set-Cookie","ESPSESSIONID=1");
			www_server.send(301);
			log(TRACE,"www Log in Successful");
			return;
			}

		msg = "Wrong username/password! try again.";
		log(TRACE,"www Log in Failed");
		}

	sprintf ( temp,
	"<html>\
<title>ME ESP8266 an MqTT Lite Instance - IoT Francesco Barone BASE v 1.0</title>\
<head>\
<link rel=\"stylesheet\" type=\"text/css\" href=\"css/admin.css\">\
\
<link rel=\"stylesheet\" href=\"css/jqm.css\" />\
<script src=\"js/jq.js\"></script>\
<script src=\"js/jqm.js\"></script>\
\
</head>");
	www_server.sendContent(String(temp));

	sprintf ( temp,
	"<body>\
<div id='main' data-role='page' style='text-align:center;'>\
  <div data-role='header'>\
    <h1>ME - Manage Module</h1>\
  </div>\
  <div data-role='main' class='ui-content'>\
    <form id='LOGIN' action='/login' method='POST'>\
    <table id='config_div'>\
    <tr><td style='vertical-align: top;'>\
    <div id='title'>Login to admin server</div>\
    <hr>");
  www_server.sendContent(String(temp));

  sprintf ( temp,
    "<div id='config-content'>\
      <table>\
      <tr><td id='COL_USERNAME' width=100px>ADMIN USER</td><td style='text-align: right;'><input maxlength='16' type=text name='USERNAME' id='USERNAME' value='%s'></td></tr>\
      <tr><td id='COL_PASSWORD' width=100px>PASSWORD</td><td style='text-align: right;'><input maxlength='16' type=password name='PASSWORD' id='PASSWORD' value='%s'></td></tr>\
      </table>\
      </td></tr>\
	  <tr><td colspan=2>\
		<h1><input type=submit name='Login' value='Login'></h1>\
	  </td></tr>\
      </table>\
    </div>\
    </td></tr>\
    </table>\
  </div>\
  <div data-role='footer' style='text-align:center;'>\
  </div>\
  </form>\
</div> \
</body>\
</html>",DEFAULT_ADMIN_USER,DEFAULT_ADMIN_PASSWORD);

  www_server.sendContent(String(temp));
  www_server.client().stop();

  wdt_enable(25000);
	}

void ME::handle_index_internal(){

  char temp[2048];

  //wdt_disable();

  log(TRACE,"Serve internal index.html");

  www_server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  www_server.sendHeader("Pragma", "no-cache");
  www_server.sendHeader("Expires", "-1");
  www_server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  www_server.send(200, "text/html","");

  sprintf ( temp,
    "<html>\
<title>ME ESP8266 an MqTT Lite Instance - IoT Francesco Barone BASE v 1.0</title>\
<head>\
</head>\
<body>\
<div data-role='page'>\
  <div data-role='header'>\
    <h1>Config Module</h1>\
  </div>\
  <div data-role='main' class='ui-content'>\
    <form id='SET_CONFIG' action='/action/set_config' method='get'>\
    <table id='config_div'>\
    <tr><td style='vertical-align: top;'>\
    <div id='title'>Title</div>\
    <hr>");
  www_server.sendContent(temp);

  sprintf ( temp,
    "<div id='config-content'>\
      <table>\
      <tr><td id='COL_SRVNAME' width=100px>SERVER NAME</td><td style='text-align: right;'><input maxlength='16' type=text name='SRVNAME' id='SRVNAME' value='MqTT'></td></tr>\
      <tr><td id='COL_SRVPORT' width=100px>SERVER PORT</td><td style='text-align: right;'><input maxlength='16' type=text name='SRVPORT' id='SRVPORT' value='1883'></td></tr>\
	  \
      <tr><td id='COL_SSID_AP' width=100px>SSID AP</td><td style='text-align: right;'><input type=text name='AP_SSID' id='AP_SSID' value='MqTT'></td></tr>\
      <tr><td id='COL_PASSWORD_AP' width=100px>PASSWORD</td><td style='text-align: right;'><input type=text name='AP_PASSWORD' id='AP_PASSWORD' value='passw0rd'></td></tr>\
      <tr><td id='COL_IP' width=100px>IP Address</td><td style='text-align: right;'>");
   www_server.sendContent(temp);

   sprintf ( temp,
      "<table><tr>\
      <td>\
      <div class='ui-block-a'>\
        <input data-inline='true' style='width:50px' maxlength='3' type=text name='IP1' id='IP1' value='192'>\
      </div>\
      </td>\
      <td>\
      <div class='ui-block-a'>\
        <input data-inline='true' style='width:50px' maxlength='3' type=text name='IP2' id='IP2' value='168'>\
      </div>\
      </td>\
      <td>\
      <div class='ui-block-a'>\
        <input data-inline='true' style='width:50px' maxlength='3' type=text name='IP3' id='IP3' value='4'>\
      </div>\
      </td>");
    www_server.sendContent(temp);

    sprintf ( temp,
      "<td>\
      <div class='ui-block-a'>\
        <input data-inline='true' style='width:50px' maxlength='3' type=text name='IP4' id='IP4' value='1'>\
      </div>\
      </td>\
      </tr>\
      </table>\
      </td></tr>\
      </table>\
    </div>\
    </td></tr>\
    </table>\
  </div>\
  <div data-role='footer'>\
    <h1><input type=submit name='Save Config'></h1>\
  </div>\
  </form>\
</div> \
</body>\
</html>");
  www_server.sendContent(temp);

  //content len not specified
  www_server.client().stop();
  //www_server.send(200, "text/html", temp);

  //wdt_enable(5000);
	}

void ME::handle_admin_user_internal(){

	char temp[1024];
	int rc=0;

	log(TRACE,"Process admin_user");
	if (www_server.args() > 0 ) {

		String USERNAME="",USERPWD="";

		for ( uint8_t i = 0; i < www_server.args(); i++ ) {

			log(TRACE,www_server.argName(i) +"="+www_server.arg(i));
			if (www_server.argName(i) == "ADMIN_USER")USERNAME=www_server.arg(i);
			if (www_server.argName(i) == "ADMIN_PASSWORD")USERPWD=www_server.arg(i);
			}

		if(USERNAME!="" && USERPWD!=""){

			log(TRACE,"Update admin user set Username="+USERNAME);

			ADMIN_USER=USERNAME;
			ADMIN_PASSWORD=USERPWD;

			save_field_EEPROM(98,ADMIN_USER,8);
			save_field_EEPROM(106,ADMIN_PASSWORD,8);
			}
		}

	sprintf ( temp,

	  "<html>\
		<head>\
		  <title>ESP8266 ME-MqTT Lite</title>\
		  <style>\
			body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
		  </style>\
		</head>\
		<body>\
		  <h1>Request processed with RC=%d, Return to <a href=\"/admin.htm\">admin page</a></h1>\
		</body>\
	  </html>",rc);

	www_server.send(200, "text/html", temp);
	}

void ME::handle_set_config_internal(){

  char temp[1024];
  int rc=0;

  log(TRACE,"Process set_config");
  if (www_server.args() > 0 ) {

    String SRVNAME = "";
    String SRVPORT = "";
    String AP_SSID = "";
    String AP_PASSWORD = "";
    String IP1 = "192";
    String IP2 = "168";
    String IP3 = "4";
    String IP4 = "1";

    for ( uint8_t i = 0; i < www_server.args(); i++ ) {

      log(TRACE,www_server.argName(i) +"="+www_server.arg(i));
      if (www_server.argName(i) == "SRVNAME")SRVNAME=www_server.arg(i);
      if (www_server.argName(i) == "SRVPORT")SRVPORT=www_server.arg(i);
      if (www_server.argName(i) == "AP_SSID")AP_SSID=www_server.arg(i);
      if (www_server.argName(i) == "AP_PASSWORD")AP_PASSWORD=www_server.arg(i);
      if (www_server.argName(i) == "IP1")IP1=www_server.arg(i);
      if (www_server.argName(i) == "IP2")IP2=www_server.arg(i);
      if (www_server.argName(i) == "IP3")IP3=www_server.arg(i);
      if (www_server.argName(i) == "IP4")IP4=www_server.arg(i);
	  if (www_server.argName(i) == "SECURE" && www_server.arg(i)=="enabled")USER_AUTH=true;
      }

    rc=-1;
    if(SRVNAME!="" && SRVPORT!=""){

      log(TRACE,"Save to EEPROM");

      //-------------
      //0   1    	     17     49      65          81        93     97   98		  106		  114	 115
	  //CFG DEVICENAME SERIAL AP_SSID AP_PASSWORD IPADDRESS MQTTPORT CRC  ADMIN USER  ADMIN PSW	  SECURE

      save_field_EEPROM(1,SRVNAME,16);
      save_field_EEPROM(49,AP_SSID,16);
      save_field_EEPROM(65,AP_PASSWORD,16);
      save_field_EEPROM(81,IP1,3);
      save_field_EEPROM(84,IP2,3);
      save_field_EEPROM(87,IP3,3);
      save_field_EEPROM(90,IP4,3);
      save_field_EEPROM(93,SRVPORT,4);

	  save_field_EEPROM(97,"9",1);

	  //mqtt management
	  save_field_EEPROM(98,DEFAULT_ADMIN_USER,8);
	  save_field_EEPROM(106,DEFAULT_ADMIN_PASSWORD,8);

	  //2017.07.18
	  if(USER_AUTH)save_field_EEPROM(114,"1",1);
	  if(!USER_AUTH)save_field_EEPROM(114,"0",1);

      //generate device serial
      {
      byte mac[6];
      char deviceserial[32];
      WiFi.macAddress(mac);
      sprintf(deviceserial,"%02x:%02x:%02x:%02x:%02x:%02x",mac[5],mac[4],mac[3],mac[2],mac[1],mac[0]);

      save_field_EEPROM(17,deviceserial,32);
      }

      save_field_EEPROM(0,"1",1);
      rc=0;
      }
    }

  sprintf ( temp,

  "<html>\
    <head>\
      <title>ESP8266 ME-MqTT Lite</title>\
      <style>\
        body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
      </style>\
    </head>\
    <body>\
      <h1>Request processed with RC=%d, please reboot!</h1>\
    </body>\
  </html>",rc);

  www_server.send(200, "text/html", temp);
  }

void ME::handleNotFound_internal(){

	if(loadFromSPIFFS(www_server.uri())) return;
	String message = "Internal Storage Fails\n\n";
	message += "URI: ";
	message += www_server.uri();
	message += "\nMethod: ";
	message += (www_server.method() == HTTP_GET)?"GET":"POST";
	message += "\nArguments: ";
	message += www_server.args();
	message += "\n";
	for (uint8_t i=0; i<www_server.args(); i++){
		message += " NAME:"+www_server.argName(i) + "\n VALUE:" + www_server.arg(i) + "\n";
		}
	www_server.send(404, "text/plain", message);
	log(TRACE,message);
	}

bool ME::loadFromSPIFFS(String path){

	if(!path.startsWith("/"))prefix += "/";

	if(index_page=="")index_page="index.htm";

	String dataType = "text/plain";
	if(path.endsWith("/")) path += index_page;

	//check secured page
	if(path.endsWith("admin.htm")){

		if (!is_authentified()){

			www_server.sendHeader("Location","/login");
			www_server.sendHeader("Cache-Control","no-cache");
			www_server.send(301);
			return true;
			}
		}

	if(path.endsWith(".src")) path = path.substring(0, path.lastIndexOf("."));
	else if(path.endsWith(".htm")) dataType = "text/html";
	else if(path.endsWith(".css")) dataType = "text/css";
	else if(path.endsWith(".js")) dataType = "application/javascript";
	else if(path.endsWith(".png")) dataType = "image/png";
	else if(path.endsWith(".gif")) dataType = "image/gif";
	else if(path.endsWith(".jpg")) dataType = "image/jpeg";
	else if(path.endsWith(".ico")) dataType = "image/x-icon";
	else if(path.endsWith(".xml")) dataType = "text/xml";
	else if(path.endsWith(".pdf")) dataType = "application/pdf";
	else if(path.endsWith(".zip")) dataType = "application/zip";

	log(TRACE,"Try to serve "+prefix+path.c_str());
	File dataFile = SPIFFS.open(prefix+path.c_str(),"r");

	if (!dataFile)
		return false;

	//2017.07.18
	ESP.wdtFeed();

	if (www_server.hasArg("download")) dataType = "application/octet-stream";

	wdt_disable();

	if (www_server.streamFile(dataFile, dataType) != dataFile.size()) {
		log(TRACE,"Sent less data than expected!");
		}

	dataFile.close();

	wdt_enable(8000);

	return true;
	}

//LOG
void ME::log(uint8_t level, String payload){

	if(level<=LOG_LEVEL){

	  serial->printf("[%4d] ",STATE);
	  serial->println(payload);
	  }

	}

void ME::setloglevel(uint8_t level){

	LOG_LEVEL = level;
	}
