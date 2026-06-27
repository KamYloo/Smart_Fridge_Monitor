#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <MFRC522.h>

const char* ssid = "StumilowyLas";
const char* password = "netlab123";
const char* mqtt_server = "192.168.220.1";

const String AUTHORIZED_UID = "BB:21:91:22"; 

#define SS_PIN 5 
#define RST_PIN 22
#define LIGHT_PIN 34

const char* TOPIC_ALARM  = "lodowka/alarm";
const char* TOPIC_STATUS = "lodowka/status";
const char* TOPIC_LWT    = "lodowka/wezel1/status";

MFRC522 rfid(SS_PIN, RST_PIN);
WiFiClient espClient;
PubSubClient client(espClient);

bool cardPresented = false;
unsigned long cardTime = 0;
const unsigned long CARD_VALID_MS = 15000;

bool lastBoxOpenState = false; 

String szyfrujJSON(String tekst, String klucz) {
  String wynik = "";
  for (int i = 0; i < tekst.length(); i++) {
    char c = tekst[i] ^ klucz[i % klucz.length()]; 
    if(c < 16) wynik += "0"; 
    wynik += String(c, HEX); 
  }
  return wynik;
}

void connectWiFi() {
  WiFi.begin(ssid, password);
  Serial.print("Łączę z WiFi");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println(" Połączono!");
}

void connectMQTT() {
  while (!client.connected()) {
    Serial.print("Łączę MQTT...");
    if (client.connect("ESP32_Wezel1", TOPIC_LWT, 1, true, "OFFLINE")) {
      Serial.println("OK");
      client.publish(TOPIC_LWT, "ONLINE", true);
    } else {
      delay(3000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  SPI.begin();
  rfid.PCD_Init();
  connectWiFi();
  client.setServer(mqtt_server, 1883);
  connectMQTT();
}

void loop() {
  if (!client.connected()) connectMQTT();
  client.loop();

  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    String uid = "";
    for (byte i = 0; i < rfid.uid.size; i++) {
      if (rfid.uid.uidByte[i] < 0x10) uid += "0";
      uid += String(rfid.uid.uidByte[i], HEX);
      if (i < rfid.uid.size - 1) uid += ":";
    }
    uid.toUpperCase();
    Serial.printf("Karta: %s\n", uid.c_str());

    StaticJsonDocument<128> doc;
    char buf[128];
    if (uid == AUTHORIZED_UID) {
      cardPresented = true;
      cardTime = millis();
      doc["event"] = "card_ok";
      doc["uid"] = uid;
      doc["info"] = "Zamek odblokowany na 15s";
      serializeJson(doc, buf);
      
      client.publish(TOPIC_STATUS, szyfrujJSON(String(buf), "INZYNIER2024").c_str());
    } else {
      doc["alarm"] = "NIEZNANA KARTA";
      doc["uid"] = uid;
      serializeJson(doc, buf);
      
      client.publish(TOPIC_ALARM, szyfrujJSON(String(buf), "INZYNIER2024").c_str());
    }
    rfid.PICC_HaltA();
  }

  int lightVal = analogRead(LIGHT_PIN);
  bool boxOpen = (lightVal > 2000);

  if (boxOpen != lastBoxOpenState) {
    StaticJsonDocument<128> doc;
    char buf[128];
    if (boxOpen) {
      doc["event"] = "drzwi_otwarte";
      doc["swiatlo"] = lightVal;
      if (cardPresented && (millis() - cardTime < CARD_VALID_MS)) {
        doc["autoryzacja"] = "TAK";
      } else {
        doc["autoryzacja"] = "BRAK";
      }
    } else {
      doc["event"] = "drzwi_zamkniete";
      doc["swiatlo"] = lightVal;
    }
    serializeJson(doc, buf);
    
    client.publish(TOPIC_STATUS, szyfrujJSON(String(buf), "INZYNIER2024").c_str());
    lastBoxOpenState = boxOpen;
  }

  if (boxOpen) {
    bool cardValid = cardPresented && (millis() - cardTime < CARD_VALID_MS); 
    
    if (!cardValid) {
      StaticJsonDocument<128> doc;
      doc["alarm"] = "NIEAUTORYZOWANE OTWARCIE LUB DRZWI ZBYT DLUGO OTWARTE!";
      doc["swiatlo"] = lightVal;
      char buf[128];
      serializeJson(doc, buf);
      
      client.publish(TOPIC_ALARM, szyfrujJSON(String(buf), "INZYNIER2024").c_str());
      delay(3000);
    }
  } else {
    if (cardPresented && (millis() - cardTime > CARD_VALID_MS)) {
      cardPresented = false;
      StaticJsonDocument<128> doc;
      char buf[128];
      doc["event"] = "autoryzacja_wygasla";
      doc["info"] = "Zamek lodowki uzbrojony ponownie";
      serializeJson(doc, buf);
      
      client.publish(TOPIC_STATUS, szyfrujJSON(String(buf), "INZYNIER2024").c_str());
    }
  }
  delay(200);
}