/*

  Bestway Spa Controller Library by Thomas Landahl (Visualapproach)

  https://github.com/visualapproach/WiFi-remote-for-Bestway-Lay-Z-SPA

  Installation and first run:

Use PlatformIO in Visual Studio Code:
  Compile and upload sketch.
  **Upload LittleFS image.**

  An Access Point is created called "Auto portal". Log in and enter wifi credentials.

  Visit IP/ and click "Go to config page"
  Enter your settings, click SAVE.

  This version is using a command queue with repeatable commands.
  11 commands can be queued, of which 10 should be used by the user.
  Please leave 1 for internal use.
  The library will unlock the device automatically if necessary.
  Just send what you want to happen when, and the library will take care of it.
  You can even directly set desired target temperature!!

  LittleFS files saved from this program:

  settings.txt   - stores JSON formatted running times and configurations. Updates every 2 hrs.
  cmdq.txt       - stores JSON formatted command queue.
  eventlog.txt   - stores JSON formatted states and a timestamp (Mostly for debugging. Not human friendly as is)
  bootlog.txt    - stores JSON formatted boot times and reasons.

  These files can be read by visiting the address "IP/filename.txt"
  You can if you want edit the file and upload it by visiting "IP/upload.html"

    -------------------Info to coders below--------------------

  Public functions in BWC class:

  void begin(void);       //Must be run once in setup.
  void loop(void);        //As always, don't use blocking code. This needs to be run fairly frequently.
  bool qCommand(uint32_t cmd, uint32_t val, uint32_t xtime, uint32_t interval);


  bool newData();                         //true when a state has changed
  void saveEventlog();                    //save states to LittleFS (flash) (I don't recommend using this unless you are really a log freak)
  String getJSONStates();                 //get a JSON formatted string to send to clients
  String getJSONTimes();                  //get a JSON formattes string to send to clients
  String getJSONSettings();               //get a JSON formattes string to send to clients (internal use)
  void setJSONSettings(String message);   //save settings to flash. Must be formatted correctly. (internal use)
  String getJSONCommandQueue();           //get a JSON formatted string to send to clients

  States index constants, for use in bwc.getState(int state):
  0  LOCKEDSTATE
  1  POWERSTATE
  2  UNITSTATE
  3  BUBBLESSTATE
  4  HEATGRNSTATE
  5  HEATREDSTATE
  6  HEATSTATE
  7  PUMPSTATE
  8  TARGET
  9  TEMPERATURE
  10 CHAR1
  11 CHAR2
  12 CHAR3
  13 JETSSTATE

  These are defined in the library so you can call bwc.getState(TEMPERATURE) for instance.

  Commands index constants, for use in bwc.qCommand(uint32_t cmd, uint32_t val, uint32_t xtime, uint32_t interval)
  (Sending commands from web pages or MQTT)
  cmd:
  0 SETTARGET
  1 SETUNIT
  2 SETBUBBLES
  3 SETHEATER
  4 SETPUMP
  5 RESETQ
  6 REBOOTESP
  7 GETTARGET
  8 RESETTIMES
  9 RESETCLTIMER
    ...may be extended

  val:
    1 = ON/Fahrenheit, 0 = OFF/Celsius, or any value for SETTARGET temperature
  xtime: eXecute at timestamp (unix timestamp in seconds)
  interval: repeat every INTERVAL seconds. 0 = no repeat.

  These are defined in the library so you can call bwc.qCommand(SETPUMP, 1, 1603321200, 3600) to
  turn on filter pump at 10/21/2020 @ 11:00pm, repeating every hour (3600s)

  The library is running an NTP and LittleFS so no need to include those here

*/

#include "4_wire.h"
#include <Arduino.h>

Ticker updateMqttTimer;
Ticker updateWSTimer;
BWC bwc;
bool sendWSFlag = false;

void setup()
{
  Serial.begin(115200);
  bwc.begin(); //no params = default pins
  startWiFi();
  startOTA();
  startServer();
  startWebSocket();
  
  updateWSTimer.attach(2.0, sendWSsetFlag);     //update webpage every 2 secs plus state changes

  pinMode(D4, OUTPUT);  //built in LED for some feedback
  digitalWrite(D4, LOW);
}

void loop() 
{
  webSocket.loop();             // constantly check for websocket events
  server.handleClient();        // run the server
  ArduinoOTA.handle();          // listen for OTA events
  bwc.loop();                   // Fiddle with the pump computer
  if (bwc.newData()) {
    sendMessage(1);//ws
    //sendMessage(0);//mqtt
    //bwc.saveEventlog();       //will only fill up and wear flash memory eventually
  }
  if (sendWSFlag) {
    sendWSFlag = false;
    sendMessage(1);//ws
  }

  //  //Usage example. Solar panels are giving a (3.3V) signal to start dumping electricity into the pool heater.
  //  //Rapid changes on this pin will fill up the command queue and stop you from adding other commands
  //  //It will also cause the heater to turn on and off as fast as it can
  //  //I have not tested this feature, but what can go wrong ;-)
  //  if (digitalRead(solarpin) == HIGH && runonce == true) {
  //    bwc.qCommand(SETHEATER, 1, 0, 0);       // cmd:set heater, to ON (1), immidiately, no repeat
  //    runonce = false;                        // to stop queing commands every loop. We only want to trigger once
  //  }
  //  if (digitalRead(solarpin) == LOW && runonce == false) {
  //    bwc.qCommand(SETPUMP, 0, 0, 0);         // change to SETHEATER if you want the pump to continue filtering
  //    runonce = true;
  //  }
  //
  //  //Switch the output pin when temperature is below or above 30
  //  //Don't load the pin above specs (a few mA)
  //  if (bwc.getState(TEMPERATURE) < 30){
  //    digitalWrite(myoutputpin, HIGH);
  //  } else {
  //    digitalWrite(myoutputpin, LOW);
  //  }

  //You can add own code here, but don't stall! If CPU is choking you can try to run @ 160 MHz, but that's cheating!
}

void sendWSsetFlag() {
  sendWSFlag = true;
}

// Send status data to web client in JSON format (because it is easy to decode on the other side)
void sendMessage(int msgtype) {
  String jsonmsg = bwc.getJSONStates();

  //send states to web sockets
  if (msgtype == 1) {
    webSocket.broadcastTXT(jsonmsg);
  }

  jsonmsg = bwc.getJSONTimes();

  if (msgtype == 1) {
    webSocket.broadcastTXT(jsonmsg);
  }
}

/*
   File handlers below. Most users can stop reading here.
*/

String getContentType(String filename) { // determine the filetype of a given filename, based on the extension
  if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  else if (filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

bool handleFileRead(String path) { // send the right file to the client (if it exists)
  ////Serial.println("handleFileRead: " + path);
  if (path.endsWith("/")) path += F("index.html");          // If a folder is requested, send the index file
  String contentType = getContentType(path);             // Get the MIME type
  String pathWithGz = path + ".gz";
  if (LittleFS.exists(pathWithGz) || LittleFS.exists(path)) { // If the file exists, either as a compressed archive, or normal
    if (LittleFS.exists(pathWithGz))                         // If there's a compressed version available
      path += ".gz";                                         // Use the compressed version
    File file = LittleFS.open(path, "r");                    // Open the file
    server.streamFile(file, contentType);    // Send it to the client
    file.close();                                          // Close the file again
    ////Serial.println(String("\tSent file: ") + path);
    return true;
  }
 // //Serial.println(String("\tFile Not Found: ") + path);   // If the file doesn't exist, return false
  return false;
}

void handleNotFound() { // if the requested file or page doesn't exist, return a 404 not found error
  if (!handleFileRead(server.uri())) {        // check if the file exists in the flash memory (LittleFS), if so, send it
    server.send(404, "text/plain", "404: File Not Found");
  }
}

void handleFileUpload() { // upload a new file to the LittleFS
  HTTPUpload& upload = server.upload();
  String path;
  if (upload.status == UPLOAD_FILE_START) {
    path = upload.filename;
    if (!path.startsWith("/")) path = "/" + path;
    if (!path.endsWith(".gz")) {                         // The file server always prefers a compressed version of a file
      String pathWithGz = path + ".gz";                  // So if an uploaded file is not compressed, the existing compressed
      if (LittleFS.exists(pathWithGz))                     // version of that file must be deleted (if it exists)
        LittleFS.remove(pathWithGz);
    }
    ////Serial.print(F("handleFileUpload Name: ")); //Serial.println(path);
    fsUploadFile = LittleFS.open(path, "w");            // Open the file for writing in LittleFS (create if it doesn't exist)
    path = String();
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize); // Write the received bytes to the file
  } else if (upload.status == UPLOAD_FILE_END) {
    if (fsUploadFile) {                                   // If the file was successfully created
      fsUploadFile.close();                               // Close the file again
      ////Serial.print(F("handleFileUpload Size: ")); //Serial.println(upload.totalSize);
      server.sendHeader("Location", "/success.html");     // Redirect the client to the success page
      server.send(303);
    } else {
      server.send(500, "text/plain", "500: couldn't create file");
    }
  }
}

/*
   Starters - bon apetit
*/

void startWebSocket() { // Start a WebSocket server
  webSocket.begin();                          // start the websocket server
  webSocket.onEvent(webSocketEvent);          // if there's an incomming websocket message, go to function 'webSocketEvent'
  ////Serial.println(F("WebSocket server started."));
}

void startServer() { // Start a HTTP server with a file read handler and an upload handler
  server.on(F("/upload.html"),  HTTP_POST, []() {  // If a POST request is sent to the /upload.html address,
    server.send(200, "text/plain", "");
  }, handleFileUpload);                       // go to 'handleFileUpload'

  server.on(F("/getconfig/"), handleGetConfig);
  server.on(F("/setconfig/"), handleSetConfig);
  server.on(F("/getcommands/"), handleGetCommandQueue);
  server.on(F("/addcommand/"), handleAddCommand);
  server.on("/version", HTTP_GET, []() {server.send(200, "text/plain", MY_VERSION);});
  //  server.on(F("/remove.html"), handleLogRemove);  //not implemented
  server.on("/update.html", HTTP_POST, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.setDebugOutput(true);
      WiFiUDP::stopAll();
      Serial.printf("Update: %s\n", upload.filename.c_str());
      uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
      if (!Update.begin(maxSketchSpace)) { //start with max available size
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) { //true to set the size to the current progress
        Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
        server.sendHeader("Location", "/success.html");     // Redirect the client to the success page
        server.send(303);
      } else {
        Update.printError(Serial);
      }
      Serial.setDebugOutput(false);
    }
    yield();
  });

  server.onNotFound(handleNotFound);          // if someone requests any other file or page, go to function 'handleNotFound'
  // and check if the file exists

  server.begin();                             // start the HTTP server
  Serial.println(F("HTTP server started."));
}

void startOTA() { // Start the OTA service
  ArduinoOTA.setHostname(OTAName);
  ArduinoOTA.setPassword(OTAPassword);

  ArduinoOTA.onStart([]() {
    //Serial.println(F("OTA Start"));
  });
  ArduinoOTA.onEnd([]() {
    //Serial.println(F("\r\nOTA End"));
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    //Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    //Serial.printf("Error[%u]: ", error);
    // if (error == OTA_AUTH_ERROR) //Serial.println(F("Auth Failed"));
    // else if (error == OTA_BEGIN_ERROR) //Serial.println(F("Begin Failed"));
    // else if (error == OTA_CONNECT_ERROR) //Serial.println(F("Connect Failed"));
    // else if (error == OTA_RECEIVE_ERROR) //Serial.println(F("Receive Failed"));
    // else if (error == OTA_END_ERROR) //Serial.println(F("End Failed"));
  });
  ArduinoOTA.begin();
  //Serial.println(F("OTA ready\r\n"));
}

void startWiFi()
{
  // Start a Wi-Fi access point, and try to connect to some given access points. Then wait for either an AP or STA connection
  WiFi.setHostname("ESP-LayZ");
  WiFi.mode(WIFI_STA);
  
  WiFiManager wm;
  wm.autoConnect("ESP-LayZ-AutoPortal");

  while (WiFi.status() != WL_CONNECTED) {  // Wait for the Wi-Fi to connect
    delay(250);
  }
  
  //Serial.println("\r\n");
  //Serial.print(F("Connected to "));
  //Serial.println(WiFi.SSID());             // Tell us what network we're connected to
  //Serial.print(F("IP address:\t"));
  //Serial.print(WiFi.localIP());            // Send the IP address of the ESP8266 to the computer
  //Serial.println("\r\n");
}

/*
   Web server functions to exchange data between server and web client
*/

//response to /getconfig/
void handleGetConfig() { // reply with json document
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
  } else {
    String jsonmsg = bwc.getJSONSettings();
    server.send(200, "text/plain", jsonmsg);
  }
}

//response to /setconfig/
void handleSetConfig() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
  } else {
    String message = server.arg(0);
    bwc.setJSONSettings(message);
    server.send(200, "plain/text", "");
  }
}


//response to /getcommands/
void handleGetCommandQueue() { // reply with json document
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
  } else {
    String jsonmsg = bwc.getJSONCommandQueue();
    server.send(200, "text/plain", jsonmsg);
  }
}

//respone to /addcommand/
void handleAddCommand() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
  } else {
    DynamicJsonDocument doc(256);
    String message = server.arg(0);
    DeserializationError error = deserializeJson(doc, message);
    if (error) {
      server.send(500, "text/plain", "");
      return;
    }
    uint32_t command = doc["CMD"];
    uint32_t value = doc["VALUE"];
    uint32_t xtime = doc["XTIME"];
    uint32_t interval = doc["INTERVAL"];
    bwc.qCommand(command, value, xtime, interval);
    server.send(200, "text/plain", "");
  }
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t len) {
  // When a WebSocket message is received
  switch (type) {
    case WStype_DISCONNECTED:             // if the websocket is disconnected
      //Serial.printf("[%u] Disconnected!\n", num);
      break;
    case WStype_CONNECTED: {              // if a new websocket connection is established
        //IPAddress ip = webSocket.remoteIP(num);
        //Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
        sendMessage(1);
      }
      break;
    case WStype_TEXT:                     // if new text data is received
      {
      //Serial.printf("[%u] get Text: %s\n", num, payload);
      DynamicJsonDocument doc(256);
      DeserializationError error = deserializeJson(doc, payload);
      if (error) {
        //Serial.println(F("JSON command failed"));
        return;
      }

      // Copy values from the JsonDocument to the Config
      uint32_t command = doc["CMD"];
      uint32_t value = doc["VALUE"];
      uint32_t xtime = doc["XTIME"];
      uint32_t interval = doc["INTERVAL"];
      //add command to the command queue
      bwc.qCommand(command, value, xtime, interval);
      }
      break;
    default:
      break;
  }
}
