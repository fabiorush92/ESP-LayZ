#define MY_VERSION "BW4W_1.0"

#include "Arduino.h"
#include "BWC_8266_4w.h"
#include <WiFiUdp.h>
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <ESP8266WebServer.h>
//#include <LittleFS.h>
#include <WebSocketsServer.h>
//used by ap config portal
#include <DNSServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
//#include <Ticker.h>

ESP8266WebServer server(80);       // Create a webserver object that listens for HTTP request on port 80
WebSocketsServer webSocket(81);    // create a websocket server on port 81
bool portalRunning = false;
File fsUploadFile;                 // a File variable to temporarily store the received file

//OTA credentials
const char *OTAName = MY_VERSION;           // A name and a password for the OTA service
const char *OTAPassword = "esp8266";        //myOTApassword;

WiFiClient My_WiFi_Client;
// If failing to connect at all, this will stall the loop = Erratic behavior may occur if called to often.
// Default frequency of once every 10 min shouldn't be a problem though.

void sendWSsetFlag();
// Send status data to web client in JSON format (because it is easy to decode on the other side)
void sendMessage(int msgtype);
/*
   File handlers below. Most users can stop reading here.
*/
String getContentType(String filename);
bool handleFileRead(String path);
void handleNotFound();
void handleFileUpload(); 
/*
   Starters - bon apetit
*/
void startWebSocket();
void startServer();
void startOTA();
void startWiFi();
/*
   Web server functions to exchange data between server and web client
*/
//response to /getconfig/
void handleGetConfig();
//response to /setconfig/
void handleSetConfig();
//response to /getcommands/
void handleGetCommandQueue();
//respone to /addcommand/
void handleAddCommand();
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t len);