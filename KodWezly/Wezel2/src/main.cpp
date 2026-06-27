#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Adafruit_BME680.h>
#include <Wire.h>

const char* ssid = "StumilowyLas";
const char* password = "netlab123";
const char* mqtt_server = "192.168.220.1";

const char* TOPIC_SENSORS = "lodowka/sensory";
const char* TOPIC_LWT     = "lodowka/wezel2/status";

Adafruit_BME680 bme;
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
  Serial.print("Laczenie z siecia Wi-Fi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print("."); 
  }
  
  Serial.println("\nPolaczono z Wi-Fi!");
  Serial.print("Adres IP ESP32: ");
  Serial.println(WiFi.localIP());
}

void connectMQTT() {
  while (!client.connected()) {
    Serial.print("Proba polaczenia z MQTT... ");
    if (client.connect("ESP32_Wezel2", TOPIC_LWT, 1, true, "OFFLINE")) {
      Serial.println("Polaczono!");
      client.publish(TOPIC_LWT, "ONLINE", true);
    } else {
      Serial.print("Blad: ");
      Serial.print(client.state());
      Serial.println(" -> kolejna proba za 3 sekundy...");
      delay(3000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin();
  if (!bme.begin()) {
    Serial.println("Nie znaleziono BME680!");
    while (1);
  }
  bme.setTemperatureOversampling(BME680_OS_8X);
  bme.setHumidityOversampling(BME680_OS_2X);
  bme.setPressureOversampling(BME680_OS_4X);
  bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
  bme.setGasHeater(320, 150);
  connectWiFi();
  client.setServer(mqtt_server, 1883);
  connectMQTT();
}

void loop() {
  if (!client.connected()) connectMQTT();
  client.loop();

  if (bme.performReading()) {
    StaticJsonDocument<200> doc;
    doc["urzadzenie"]  = "lodowka_bme";
    doc["temperatura"] = round(bme.temperature * 10.0) / 10.0;
    doc["wilgotnosc"]  = round(bme.humidity * 10.0) / 10.0;
    doc["cisnienie"]   = round(bme.pressure / 100.0 * 10.0) / 10.0;
    doc["gaz_voc"]     = bme.gas_resistance / 1000.0;
    doc["status"]      = "OK";
    char buf[200];
    serializeJson(doc, buf);
    
    String zaszyfrowane = szyfrujJSON(String(buf), "INZYNIER2024");
    client.publish(TOPIC_SENSORS, zaszyfrowane.c_str());
    
    Serial.print("Wyslano czysty JSON: ");
    Serial.println(buf);
    Serial.print("Wyslano w siec (HEX): ");
    Serial.println(zaszyfrowane);
  } else {
    Serial.println("UWAGA: Komunikacja I2C dziala, ale BME680 nie oddaje pomiarow! Sprawdz kabelki.");
  }
  
  delay(10000);
}