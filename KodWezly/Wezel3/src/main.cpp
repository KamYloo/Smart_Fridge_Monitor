#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <MPU6500_WE.h>

const char* ssid = "StumilowyLas";
const char* password = "netlab123";
const char* mqtt_server = "192.168.220.1";

const char* TOPIC_ALARM = "lodowka/alarm";
const char* TOPIC_LWT   = "lodowka/wezel3/status";

const float SHAKE_THRESHOLD = 3; 

MPU6500_WE mpu = MPU6500_WE(0x68); 

WiFiClient espClient;
PubSubClient client(espClient);

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
  while (WiFi.status() != WL_CONNECTED) { 
    delay(500); 
    Serial.print("."); 
  }
  Serial.println(" Połączono!");
}

void connectMQTT() {
  while (!client.connected()) {
    if (client.connect("ESP32_Wezel3", TOPIC_LWT, 1, true, "OFFLINE")) {
      client.publish(TOPIC_LWT, "ONLINE", true);
    } else {
      delay(3000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin();
  
  if (!mpu.init()) {
    Serial.println("Nie znaleziono czujnika MPU-6500! Sprawdź kabelki (SDA/SCL).");
    while (1);
  }
  
  Serial.println("Czujnik MPU-6500 zainicjowany poprawnie.");
  mpu.autoOffsets();
  
  connectWiFi();
  client.setServer(mqtt_server, 1883);
  connectMQTT();
}

void loop() {
  if (!client.connected()) connectMQTT();
  client.loop();

  xyzFloat gValue = mpu.getGValues();

  float totalAcc = sqrt(pow(gValue.x, 2) + pow(gValue.y, 2) + pow(gValue.z, 2));

  if (totalAcc > SHAKE_THRESHOLD) {
    StaticJsonDocument<128> doc;
    doc["alarm"]          = "WSTRZAS! Ktos kradnie lodowke!";
    doc["przyspieszenie"] = totalAcc; 
    
    char buf[128];
    serializeJson(doc, buf);
    
    String zaszyfrowane = szyfrujJSON(String(buf), "INZYNIER2024");
    client.publish(TOPIC_ALARM, zaszyfrowane.c_str());
    
    Serial.printf("ALARM! Wstrząs o sile: %.2f G\n", totalAcc);
    Serial.print("Wysłano czysty JSON: ");
    Serial.println(buf);
    Serial.print("Wysłano w sieć (HEX): ");
    Serial.println(zaszyfrowane);
    
    delay(2000); 
  }
  delay(50);
}