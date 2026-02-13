#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <MFRC522.h>
#include <map>

/* ==========================================================
   CENTRAL CONFIGURATION
   ========================================================== */
#define WIFI_SSID     "EdNet"
#define WIFI_PASS     "Huawei@123"
#define MQTT_HOST     "157.173.101.159"

#define MQTT_PORT     1883
#define TEAM_ID       "1nt3rn4l_53rv3r_3rr0r"

/* ==========================================================
   AUTO-GENERATED TOPICS
   ========================================================== */
String BASE_TOPIC   = "rfid/" + String(TEAM_ID) + "/";
String TOPIC_STATUS = BASE_TOPIC + "card/status";
String TOPIC_TOPUP  = BASE_TOPIC + "card/topup";
String TOPIC_BAL    = BASE_TOPIC + "card/balance";

/* ==========================================================
   RFID PINS
   ========================================================== */
#define SS_PIN   D8
#define RST_PIN  D3
MFRC522 rfid(SS_PIN, RST_PIN);

WiFiClient espClient;
PubSubClient client(espClient);

std::map<String, int> balances;

/* ==========================================================
   WIFI
   ========================================================== */
void setupWiFi() {
  Serial.print("\nConnecting to ");
  Serial.println(WIFI_SSID);
  
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nWiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

/* ==========================================================
   MQTT CALLBACK
   ========================================================== */
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("\nMessage arrived [");
  Serial.print(topic);
  Serial.print("]: ");
  
  String msg = "";
  for (int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }
  Serial.println(msg);

  if (String(topic) == TOPIC_TOPUP) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, msg);

    if (error) {
      Serial.print("JSON Parse failed: ");
      Serial.println(error.c_str());
      return;
    }

    int amount = doc["amount"];
    String uidKey = doc["uid"];
    balances[uidKey] += amount;
    int newBalance = balances[uidKey];

    Serial.print("Top-up received for ");
    Serial.print(uidKey);
    Serial.print("! Amount: ");
    Serial.print(amount);
    Serial.print(" | New Balance: ");
    Serial.println(newBalance);

    JsonDocument response;
    response["uid"] = doc["uid"];
    response["new_balance"] = newBalance;

    char buffer[128];
    serializeJson(response, buffer);
    client.publish(TOPIC_BAL.c_str(), buffer);
  }
}

/* ==========================================================
   MQTT RECONNECT
   ========================================================== */
void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESP8266_" + String(ESP.getChipId(), HEX);

    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      client.subscribe(TOPIC_TOPUP.c_str());
      Serial.print("Subscribed to: ");
      Serial.println(TOPIC_TOPUP);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
   
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

/* ==========================================================
   SETUP
   ========================================================== */
void setup() {
  Serial.begin(460800);
  while(!Serial); // Wait for serial port to connect
  Serial.println("\n--- ESP8266 RFID SYSTEM STARTING ---");

  SPI.begin();
  rfid.PCD_Init();
  Serial.println("RFID Reader Initialized.");

  setupWiFi();

  client.setServer(MQTT_HOST, MQTT_PORT);
  client.setCallback(mqttCallback);
}

/* ==========================================================
   LOOP
   ========================================================== */
void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    Serial.print("\nCard Detected! UID: ");
    
    String uidStr = "";
    for (byte i = 0; i < rfid.uid.size; i++) {
      uidStr += String(rfid.uid.uidByte[i] < 0x10 ? "0" : "");
      uidStr += String(rfid.uid.uidByte[i], HEX);
    }
    Serial.println(uidStr);

    JsonDocument doc;
    doc["uid"] = uidStr;
    doc["balance"] = balances[uidStr];

    char buffer[128];
    serializeJson(doc, buffer);

    Serial.print("Publishing to ");
    Serial.print(TOPIC_STATUS);
    Serial.print(": ");
    Serial.println(buffer);

    if(client.publish(TOPIC_STATUS.c_str(), buffer)) {
      Serial.println("Publish success!");
    } else {
      Serial.println("Publish failed!");
    }

    rfid.PICC_HaltA();
    delay(1000); // Debounce card reads
  }
}
