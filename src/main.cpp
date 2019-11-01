
#if !(defined(ESP_NAME))
  #define ESP_NAME "relay"
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

// LED
#include <Ticker.h>
Ticker ticker;
int ledState = LOW;
unsigned long ledNextRun = 0;

/* WIFI */
char hostname[32] = {0};

/* OSC */
WiFiUDP Udp;
OSCErrorCode error;
const unsigned int OSC_PORT = 53000;
unsigned long triggerTimeout = 0;

#if (defined(HTTP))
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
      $scope.relays = [1]

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
#endif

void receiveRelayActivate(OSCMessage &msg, int addrOffset){
  digitalWrite(D1, HIGH);
}

void receiveRelayDeactivate(OSCMessage &msg, int addrOffset){
  digitalWrite(D1, LOW);
}

void receiveRelayTrigger(OSCMessage &msg, int addrOffset){
  digitalWrite(D1, HIGH);
  triggerTimeout = millis() + 1000; // Number of milliseconds to activate relay.
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

#if (defined(HTTP))
void handleRoot() {
  server.send_P(200, "text/html", index_html);
}

void handleActivate() {
  digitalWrite(D1, HIGH);
  server.send(200);
}

void handleDeactivate() {
  digitalWrite(D1, LOW);
  server.send(200);
}

void handleTrigger() {
  digitalWrite(D1, HIGH);
  triggerTimeout = millis() + 100;
  server.send(200);
}
#endif

void tick()
{
  //toggle state
  int state = digitalRead(LED_BUILTIN);  // get the current state of GPIO1 pin
  digitalWrite(LED_BUILTIN, !state);     // set pin to the opposite state
}

void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println(F("Config Mode"));
  Serial.println(WiFi.softAPIP());
  Serial.println(myWiFiManager->getConfigPortalSSID());
  ticker.attach(0.2, tick);
}

void setup() {
  
  /* Serial and I2C */
  Serial.begin(9600);

  /* LED */
  pinMode(LED_BUILTIN, OUTPUT);

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
  digitalWrite(D1, LOW);

  /* mDNS */
  // Initialization happens inside ArduinoOTA;
  MDNS.addService(ESP_NAME, "udp", OSC_PORT);

#if (defined(HTTP))
  server.on("/", HTTP_GET, handleRoot);
  server.on("/activate", HTTP_POST, handleActivate);
  server.on("/deactivate", HTTP_POST, handleDeactivate);
  server.on("/trigger", HTTP_POST, handleTrigger);
  server.onNotFound([](){
    server.send(404, "text/plain", "404: Not found");
  });

  server.begin();                           // Actually start the server
  Serial.println("HTTP server started");
#endif
}

void loop() {
  ArduinoOTA.handle();
  receiveOSC();

#if (defined(HTTP))
  server.handleClient(); 
#endif

  if (triggerTimeout != 0 && triggerTimeout < millis()) {
    digitalWrite(D1, LOW);
    triggerTimeout = 0;
  }

  // LED
  unsigned long currentMillis = millis();
  if (ledNextRun < currentMillis) {
    if (ledState == LOW) {
      ledState = HIGH;
      ledNextRun = currentMillis + 1000;
    } else {
      ledState = LOW;
    }
    digitalWrite(LED_BUILTIN, ledState);
  }
}