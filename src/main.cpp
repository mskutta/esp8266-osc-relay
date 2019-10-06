#if !(defined(ESP_NAME))
  #define ESP_NAME "relay"
#endif

#if (defined(INVERT))
  #define ON LOW
  #define OFF HIGH 
#else
  #define ON HIGH
  #define OFF LOW 
#endif

#include <Arduino.h>

#include <ESP8266WiFi.h> // WIFI support
#include <ESP8266mDNS.h> // For network discovery
#include <WiFiUdp.h> // OSC over UDP
#include <ArduinoOTA.h> // Updates over the air

// WiFi Manager
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h> 

// OSC
#include <OSCMessage.h> // for sending OSC messages
#include <OSCBundle.h> // for receiving OSC messages

/* WIFI */
char hostname[32] = {0};

/* OSC */
WiFiUDP Udp;
OSCErrorCode error;
const unsigned int OSC_PORT = 53000;

/* Web Server */
ESP8266WebServer server(80);
const char index_html[] PROGMEM = R"=====(
  
<!DOCTYPE html>
<html ng-app='esp8266'>
<head>
  <title>Relay Control</title>
  <meta name='viewport' content='width=device-width, initial-scale=1.0'/>
  <meta charset='utf-8'>
  <style> 
    #main {display: table; margin: auto;  padding: 0 10px 0 10px; } 
    h2,{text-align:center; } 
    input[type=button] { padding:10px 10px 10px 10px; width:100%;  background-color: #4CAF50; font-size: 100%;}
  </style>
  <script src="https://ajax.googleapis.com/ajax/libs/angularjs/1.7.8/angular.min.js"></script>
  <script>
    angular.module('esp8266', [])
      .controller('esp8266', function($scope,$http) {
      $scope.relays = [1,2]

      $scope.post = function(action, relay) {
        $http({
            method : 'POST',
            url : '/' + action,
            data : 'relay=' + relay,
            headers : {
                'Content-Type' : 'application/x-www-form-urlencoded'
            }
        })
      };
    });
  </script>
</head>
<body>
  <div id='main' ng-controller='esp8266'>
    <div ng-repeat="relay in relays">
        <h2>Relay {{relay}}</h2>
        <button ng-click="post('trigger',relay)">Trigger</button>
        <button ng-click="post('activate',relay)">Activate</button>
        <button ng-click="post('deactivate',relay)">Deactivate</button>
    </div>
  </div>
</body>
</html>

)=====";

int getPin(int index) {
  switch (index) {
    case 1: return D1;
    case 2: return D2;
    // case 3: return D3;
    // case 4: return D4;
    // case 5: return D5;
    // case 6: return D6;
    // case 7: return D7;
    // case 8: return D8;
    default: return D1;
  }
}

void receiveRelayActivate(OSCMessage &msg, int addrOffset){
  if (msg.isInt(0)) {
    int pin = getPin(msg.getInt(0));
    digitalWrite(pin, ON);
  }
}

void receiveRelayDeactivate(OSCMessage &msg, int addrOffset){
  if (msg.isInt(0)) {
    int pin = getPin(msg.getInt(0));
    digitalWrite(pin, OFF);
  }
}

void receiveRelayTrigger(OSCMessage &msg, int addrOffset){
  if (msg.isInt(0)) {
    int pin = getPin(msg.getInt(0));
    digitalWrite(pin, ON);
    delay(100);
    digitalWrite(pin, OFF);
  }
}

void receiveOSC(){
  OSCMessage msg;
  int size;
  if((size = Udp.parsePacket())>0){
    while(size--)
      msg.fill(Udp.read());

    if(!msg.hasError()){
      char buffer [32];
      msg.getAddress(buffer);
      Serial.print(F("recv: "));
      Serial.println(buffer);
      
      msg.route("/relay/activate",receiveRelayActivate);
      msg.route("/relay/deactivate",receiveRelayDeactivate);
      msg.route("/relay/trigger",receiveRelayTrigger);
    } else {
      error = msg.getError();
      Serial.print(F("recv error: "));
      Serial.println(error);
    }
  }
}

void handleRoot() {
  server.send_P(200, "text/html", index_html);
}

void handleActivate() {
  if (server.hasArg("relay")) {
    int relay = server.arg("relay").toInt();
    digitalWrite(getPin(relay), ON);
    server.send(200);
  } else {
    server.send(400);
  }
}

void handleDeactivate() {
  if (server.hasArg("relay")) {
    int relay = server.arg("relay").toInt(); 
    digitalWrite(getPin(relay), OFF);
    server.send(200);
  } else {
    server.send(400);
  }
}

void handleTrigger() {
  //if (server.hasArg("relay")) {
    int relay = server.arg("relay").toInt();
    int pin = getPin(relay);
    digitalWrite(pin, ON);
    delay(100);
    digitalWrite(pin, OFF);
    server.send(200);
  //} else {
  //  server.send(400);
  //}
}

void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println(F("Config Mode"));
  Serial.println(WiFi.softAPIP());
  Serial.println(myWiFiManager->getConfigPortalSSID());
}

void setup() {
  /* Serial and I2C */
  Serial.begin(9600);

  /* Function Select */
  Serial.println(ESP_NAME);
  
  /* WiFi */
  sprintf(hostname, "%s-%06X", ESP_NAME, ESP.getChipId());
  WiFiManager wifiManager;
  wifiManager.setAPCallback(configModeCallback);
  if(!wifiManager.autoConnect(hostname)) {
    Serial.println("WiFi Connect Failed");
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(1000);
  }

  /* UDP */
  Udp.begin(OSC_PORT);

  Serial.println(hostname);
  Serial.print(F("  "));
  Serial.print(WiFi.localIP());
  Serial.print(F(":"));
  Serial.println(Udp.localPort());
  Serial.print(F("  "));
  Serial.println(WiFi.macAddress());

  /* OTA */
  ArduinoOTA.setHostname(hostname);
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println(F("End"));
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();

  pinMode(D1, OUTPUT);
  pinMode(D2, OUTPUT);
  // pinMode(D3, OUTPUT);
  // pinMode(D4, OUTPUT);
  // pinMode(D5, OUTPUT);
  // pinMode(D7, OUTPUT);
  // pinMode(D8, OUTPUT);

  digitalWrite(D1, OFF);
  digitalWrite(D2, OFF);
  // digitalWrite(D3, OFF);
  // digitalWrite(D4, OFF);
  // digitalWrite(D5, OFF);
  // digitalWrite(D6, OFF);
  // digitalWrite(D7, OFF);
  // digitalWrite(D8, OFF);

  /* mDNS */
  // Initialization happens inside ArduinoOTA;
  MDNS.addService(ESP_NAME, "udp", OSC_PORT);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/activate", HTTP_POST, handleActivate);
  server.on("/deactivate", HTTP_POST, handleDeactivate);
  server.on("/trigger", HTTP_POST, handleTrigger);
  server.onNotFound([](){
    server.send(404, "text/plain", "404: Not found");
  });

  server.begin();                           // Actually start the server
  Serial.println("HTTP server started");

}

void loop() {
  ArduinoOTA.handle();
  receiveOSC();
  server.handleClient(); 
}