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

/* WIFI */
char hostname[32] = {0};

/* OSC */
WiFiUDP Udp;
OSCErrorCode error;
const unsigned int OSC_PORT = 53000;

bool relayActivateReceived = false;
bool relayDeactivateReceived = false;
bool relayMomentaryReceived = false;

void receiveRelayActivate(OSCMessage &msg, int addrOffset){
  relayActivateReceived = true;
}

void receiveRelayDeactivate(OSCMessage &msg, int addrOffset){
  relayDeactivateReceived = true;
}

void receiveRelayMomentary(OSCMessage &msg, int addrOffset){
  relayMomentaryReceived = true;
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
      msg.route("/relay/momentary",receiveRelayMomentary);
    } else {
      error = msg.getError();
      Serial.print(F("recv error: "));
      Serial.println(error);
    }
  }
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

  /* mDNS */
  // Initialization happens inside ArduinoOTA;
  MDNS.addService(ESP_NAME, "udp", OSC_PORT);
}

void loop() {
  ArduinoOTA.handle();
  receiveOSC();

  if (relayActivateReceived) {
    relayActivateReceived = false;

    digitalWrite(D1, HIGH);
  }

  if (relayDeactivateReceived) {
    relayDeactivateReceived = false;

    digitalWrite(D1, LOW);
  }

  if (relayMomentaryReceived) {
    relayMomentaryReceived = false;

    digitalWrite(D1, HIGH);
    delay(500);
    digitalWrite(D1, LOW);
  }
}