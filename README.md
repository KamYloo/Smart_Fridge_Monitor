# 🧊 Smart Fridge Monitor — Projekt IoT (ESP32 + MQTT + Node-RED)

System zdalnego monitorowania lodówki oparty na trzech węzłach **ESP32**, komunikujących się przez **MQTT**. Dane są odszyfrowywane i przetwarzane przez **Node-RED**, który wyświetla dashboard, zapisuje historię do **SQLite** i wysyła powiadomienia przez **Telegram**.

---

## 📋 Spis treści

- [Funkcje systemu](#-funkcje-systemu)
- [Architektura](#-architektura)
- [Opis węzłów](#-opis-węzłów)
- [Szyfrowanie danych](#-szyfrowanie-danych)
- [Topiki MQTT](#-topiki-mqtt)
- [Node-RED — przepływy](#-node-red--przepływy)
- [Struktura repozytorium](#-struktura-repozytorium)
- [Wymagania sprzętowe](#-wymagania-sprzętowe)
- [Wymagania programowe](#-wymagania-programowe)
- [Instalacja i uruchomienie](#-instalacja-i-uruchomienie)
- [Konfiguracja](#-konfiguracja)

---

## ✅ Funkcje systemu

- 🔐 **Kontrola dostępu** — odczyt karty RFID odblokowuje dostęp do lodówki na 15 sekund
- 💡 **Detekcja otwarcia** — fotorezystor wykrywa czy drzwi lodówki są otwarte
- 🚨 **Alarmy** — powiadomienie przy nieautoryzowanym otwarciu lub zbyt długo otwartych drzwiach
- 🌡️ **Monitoring środowiska** — pomiar temperatury, wilgotności, ciśnienia i jakości powietrza (VOC) w lodówce co 10 sekund
- 📳 **Wykrywanie wstrząsów** — akcelerometr reaguje na próby kradzieży lub gwałtownego przesunięcia lodówki
- 📊 **Dashboard** — wizualizacja danych w czasie rzeczywistym w Node-RED UI (wykresy, wskaźniki, tekst)
- 🗄️ **Baza danych** — historia pomiarów zapisywana do SQLite
- 📱 **Telegram** — automatyczne powiadomienia alarmowe na telefon
- 🔒 **Szyfrowanie** — wszystkie wiadomości MQTT są szyfrowane XOR przed wysłaniem

---

## 🏗️ Architektura

```
  +--------------------+     +---------------------+     +--------------------+
  |      Wezel 1       |     |       Wezel 2        |     |      Wezel 3       |
  |  ESP32 + MFRC522   |     |   ESP32 + BME680     |     |  ESP32 + MPU6500   |
  |  + fotorezystor    |     |                      |     |                    |
  |                    |     |  - temperatura       |     |  - akcelerometr    |
  |  - autoryz. RFID   |     |  - wilgotnosc        |     |  - wykrycie wstrz. |
  |  - detekcja drzwi  |     |  - cisnienie         |     |                    |
  |  - alarm dostepu   |     |  - gaz VOC           |     |                    |
  +--------+-----------+     +-----------+----------+     +-----------+--------+
           |                             |                            |
           |        MQTT (szyfrowanie XOR)                           |
           +-----------------------------+----------------------------+
                                         |
                              +----------+----------+
                              |    Broker MQTT      |
                              |    (Mosquitto)      |
                              |  192.168.220.1:1883 |
                              +----------+----------+
                                         |
                              +----------+----------+
                              |      Node-RED       |
                              |                     |
                              |  - Deszyfrator XOR  |
                              |  - Dashboard UI     |
                              |  - Zapis do SQLite  |
                              |  - Telegram alerty  |
                              +---------------------+
```

---

## 🔩 Opis węzłów

### Węzeł 1 — Kontrola dostępu (RFID + LDR)

**Sensor:** MFRC522 (RFID) + fotorezystor LDR  
**Piny:** SS=GPIO5, RST=GPIO22, LDR=GPIO34 (ADC)

Węzeł pełni rolę strażnika lodówki. Czytnik RFID autoryzuje użytkownika, a fotorezystor monitoruje stan drzwi (wartość ADC > 2000 = drzwi otwarte).

**Logika działania:**

```
Karta RFID przyłożona
    ├── UID == BB:21:91:22  →  card_ok (zamek odblokowany na 15s)
    └── Inny UID            →  ALARM: NIEZNANA KARTA

Drzwi otwarte
    ├── + karta autoryzowana (w ciągu 15s)  →  event: drzwi_otwarte (autoryzacja: TAK)
    └── brak autoryzacji lub >15s           →  ALARM: NIEAUTORYZOWANE OTWARCIE

Karta wygasła (po 15s, drzwi zamknięte)
    └── event: autoryzacja_wygasla
```

**Wysyłane wiadomości MQTT (po zaszyfrowaniu XOR):**

| Topik | Zdarzenie | Przykład JSON |
|-------|-----------|---------------|
| `lodowka/status` | Karta OK | `{"event":"card_ok","uid":"BB:21:91:22","info":"Zamek odblokowany na 15s"}` |
| `lodowka/status` | Drzwi otwarte | `{"event":"drzwi_otwarte","swiatlo":2345,"autoryzacja":"TAK"}` |
| `lodowka/status` | Drzwi zamknięte | `{"event":"drzwi_zamkniete","swiatlo":450}` |
| `lodowka/status` | Autoryzacja wygasła | `{"event":"autoryzacja_wygasla","info":"Zamek lodowki uzbrojony ponownie"}` |
| `lodowka/alarm` | Nieznana karta | `{"alarm":"NIEZNANA KARTA","uid":"AB:CD:EF:12"}` |
| `lodowka/alarm` | Nieautoryzowane otwarcie | `{"alarm":"NIEAUTORYZOWANE OTWARCIE LUB DRZWI ZBYT DLUGO OTWARTE!","swiatlo":2345}` |

---

### Węzeł 2 — Monitoring środowiska (BME680)

**Sensor:** Adafruit BME680 (I2C)  
**Interfejs:** I2C (SDA/SCL)  
**Interwał:** co 10 sekund

Węzeł mierzy warunki wewnątrz lodówki i wysyła dane co 10 sekund.

**Konfiguracja sensora:**
- Oversampling temperatury: 8×
- Oversampling wilgotności: 2×
- Oversampling ciśnienia: 4×
- Filtr IIR: rozmiar 3
- Podgrzewacz gazu: 320°C przez 150 ms

**Wysyłany JSON (po zaszyfrowaniu XOR):**
```json
{
  "urzadzenie": "lodowka_bme",
  "temperatura": 4.2,
  "wilgotnosc": 68.5,
  "cisnienie": 1013.2,
  "gaz_voc": 45.3,
  "status": "OK"
}
```

---

### Węzeł 3 — Detekcja wstrząsów (MPU6500)

**Sensor:** MPU6500 (akcelerometr 3-osiowy, I2C)  
**Adres I2C:** 0x68  
**Interwał pętli:** co 50 ms

Węzeł oblicza sumaryczne przyspieszenie ze wszystkich 3 osi i uruchamia alarm gdy przekroczy próg.

**Wzór:**
```
totalAcc = sqrt(x^2 + y^2 + z^2)
jeśli totalAcc > 3G  →  ALARM WSTRZĄS
```

Po wykryciu wstrząsu węzeł czeka 2 sekundy przed kolejnym odczytem (debouncing).

**Wysyłany JSON (po zaszyfrowaniu XOR):**
```json
{
  "alarm": "WSTRZAS! Ktos kradnie lodowke!",
  "przyspieszenie": 4.87
}
```

---

## 🔒 Szyfrowanie danych

Wszystkie trzy węzły szyfrują wiadomości MQTT metodą **XOR** z kluczem `INZYNIER2024` przed wysłaniem. Wynik jest kodowany jako string szesnastkowy (HEX).

**Algorytm (identyczny w każdym węźle):**
```cpp
String szyfrujJSON(String tekst, String klucz) {
  String wynik = "";
  for (int i = 0; i < tekst.length(); i++) {
    char c = tekst[i] ^ klucz[i % klucz.length()];
    if (c < 16) wynik += "0";
    wynik += String(c, HEX);
  }
  return wynik;
}
```

Node-RED zawiera węzeł **Deszyfrator**, który odwraca tę operację przed dalszym przetwarzaniem danych.

---

## 📡 Topiki MQTT

| Topik | Producent | Konsument | Opis |
|-------|-----------|-----------|------|
| `lodowka/alarm` | W1, W3 | Node-RED | Alarmy bezpieczeństwa |
| `lodowka/status` | W1 | Node-RED | Status zamka i drzwi |
| `lodowka/sensory` | W2 | Node-RED | Dane środowiskowe BME680 |
| `lodowka/wezel1/status` | W1 | Node-RED | LWT węzła 1 (ONLINE/OFFLINE) |
| `lodowka/wezel2/status` | W2 | Node-RED | LWT węzła 2 (ONLINE/OFFLINE) |
| `lodowka/wezel3/status` | W3 | Node-RED | LWT węzła 3 (ONLINE/OFFLINE) |

> **LWT (Last Will and Testament)** — każdy węzeł publikuje `ONLINE` przy połączeniu i ustawia automatyczną wiadomość `OFFLINE` na wypadek rozłączenia. Umożliwia to monitorowanie dostępności węzłów w Node-RED.

---

## 🔀 Node-RED — przepływy

Plik `flowsNode-RED.json` zawiera jeden przepływ z następującymi elementami:

### Odbiór i deszyfrowanie

```
[mqtt in: lodowka/alarm]         --> [Deszyfrator XOR] --> [Ekstrakcja alarmu]    --> ...
[mqtt in: lodowka/sensory]       --> [Deszyfrator XOR] --> [Parsowanie temperatury]--> ...
[mqtt in: lodowka/status]        --> [Deszyfrator XOR] --> [Ekstrakcja statusu]   --> ...
[mqtt in: lodowka/wezel*/status] --> (monitoring dostepnosci wezlow)
```

### Dashboard UI

| Element | Typ | Dane |
|---------|-----|------|
| Temperatura | `ui_gauge` + `ui_text` | °C z BME680 |
| Wilgotność | `ui_gauge` + `ui_text` | % z BME680 |
| Ciśnienie | `ui_gauge` + `ui_text` | hPa z BME680 |
| Historia temperatury | `ui_chart` | dane z SQLite |
| Status drzwi | `ui_text` | event z węzła 1 |
| Alarm | `ui_text` | wiadomości alarmowe |

### Baza danych SQLite

Węzeł `Przygotuj SQL` zapisuje każdy odczyt BME680 do tabeli `pomiary`:
```sql
INSERT INTO pomiary (temperatura, wilgotnosc, cisnienie, gaz_voc, czas)
VALUES (?, ?, ?, ?, datetime('now'))
```

Węzeł `Generuj zapytanie SELECT` pobiera ostatnie 1000 rekordów do wykresu:
```sql
SELECT temperatura, czas FROM pomiary ORDER BY czas ASC LIMIT 1000
```

### Powiadomienia Telegram

Przy każdym zdarzeniu na topiku `lodowka/alarm` Node-RED wysyła wiadomość przez Telegram Bota do skonfigurowanego `chatId`.

---

## 📁 Struktura repozytorium

```
projektIOT/
|
+-- KodWezly/
|   +-- Wezel1/                    # Węzeł 1: RFID + LDR
|   |   +-- src/
|   |   |   +-- main.cpp           # Kod źródłowy ESP32
|   |   +-- platformio.ini         # Konfiguracja PlatformIO
|   |
|   +-- Wezel2/                    # Węzeł 2: BME680
|   |   +-- src/
|   |   |   +-- main.cpp
|   |   +-- platformio.ini
|   |
|   +-- Wezel3/                    # Węzeł 3: MPU6500
|       +-- src/
|       |   +-- main.cpp
|       +-- platformio.ini
|
+-- flowsNode-RED.json             # Przepływy Node-RED (import bezpośredni)
+-- .gitignore
+-- README.md
```

> ⚠️ Foldery `.pio/` (cache PlatformIO z bibliotekami) są wykluczone z repozytorium przez `.gitignore`. PlatformIO pobierze je automatycznie przy pierwszym buildzie.

---

## 🔧 Wymagania sprzętowe

### Na każdy węzeł:
- 1× **ESP32 DevKit V1** (esp32doit-devkit-v1)
- Kabel USB do programowania

### Węzeł 1:
- 1× czytnik **MFRC522** (RFID 13.56 MHz)
- 1× **fotorezystor** (LDR) + rezystor 10 kΩ (dzielnik napięcia)
- 1× karta RFID lub brelok

### Węzeł 2:
- 1× czujnik **BME680** (moduł I2C)

### Węzeł 3:
- 1× moduł **MPU6500** (I2C, adres 0x68)

### Infrastruktura:
- Router WiFi / punkt dostępowy
- Komputer z **Mosquitto** (broker MQTT)
- Komputer lub Raspberry Pi z **Node-RED**

---

## 💻 Wymagania programowe

| Oprogramowanie | Wersja | Link |
|---------------|--------|------|
| PlatformIO IDE / CLI | >= 6.x | [platformio.org](https://platformio.org) |
| Node-RED | >= 3.x | [nodered.org](https://nodered.org) |
| Mosquitto MQTT Broker | >= 2.x | [mosquitto.org](https://mosquitto.org) |
| Node-RED Dashboard (`node-red-dashboard`) | >= 3.x | npm |
| Node-RED SQLite (`node-red-node-sqlite`) | dowolna | npm |
| Node-RED Telegram (`node-red-contrib-telegrambot`) | dowolna | npm |

### Instalacja pakietów Node-RED:

```bash
npm install -g node-red-dashboard
npm install -g node-red-node-sqlite
npm install -g node-red-contrib-telegrambot
```

### Biblioteki PlatformIO (instalowane automatycznie z `platformio.ini`):

| Biblioteka | Węzeł | Wersja |
|-----------|-------|--------|
| `knolleary/PubSubClient` | W1, W2, W3 | ^2.8 |
| `bblanchon/ArduinoJson` | W1, W2, W3 | ^6.21.3 |
| `miguelbalboa/MFRC522` | W1 | ^1.4.11 |
| `adafruit/Adafruit BME680 Library` | W2 | ^2.0.2 |
| `adafruit/Adafruit Unified Sensor` | W2 | ^1.1.13 |
| `wollewald/MPU9250_WE` | W3 | ^1.2.9 |

---

## 🚀 Instalacja i uruchomienie

### 1. Klonowanie repozytorium

```bash
git clone https://github.com/TWOJ_NICK/projektIOT.git
cd projektIOT
```

### 2. Konfiguracja danych dostępowych

W pliku `src/main.cpp` **każdego węzła** zmień:

```cpp
const char* ssid        = "NAZWA_TWOJEJ_SIECI";
const char* password    = "HASLO_DO_WIFI";
const char* mqtt_server = "ADRES_IP_BROKERA_MQTT";
```

> ⚠️ Nie commituj plików z hasłami na publiczne repozytorium! Rozważ wydzielenie konfiguracji do osobnego pliku `config.h` dodanego do `.gitignore`.

### 3. Wgranie kodu na ESP32

Podłącz ESP32 przez USB, sprawdź port COM w `platformio.ini`:

```ini
upload_port = COM3              ; Windows
; upload_port = /dev/ttyUSB0             ; Linux
; upload_port = /dev/cu.usbserial-0001   ; macOS
```

Następnie dla każdego węzła:

```bash
# Węzeł 1
cd KodWezly/Wezel1
pio run --target upload

# Węzeł 2
cd ../../KodWezly/Wezel2
pio run --target upload

# Węzeł 3
cd ../../KodWezly/Wezel3
pio run --target upload
```

Lub otwórz każdy folder oddzielnie w **VS Code z rozszerzeniem PlatformIO** i kliknij przycisk **→ Upload**.

### 4. Uruchomienie brokera MQTT

```bash
mosquitto -v
```

Domyślnie broker nasłuchuje na porcie `1883`.

### 5. Uruchomienie Node-RED

```bash
node-red
```

Otwórz przeglądarkę: `http://localhost:1880`

### 6. Import przepływów Node-RED

1. Kliknij ikonę menu (☰) w prawym górnym rogu
2. Wybierz **Import**
3. Kliknij **select a file to import** i wskaż `flowsNode-RED.json`
4. Kliknij **Import**, a następnie **Deploy** (czerwony przycisk)

### 7. Dashboard

Po uruchomieniu wszystkich węzłów dashboard dostępny jest pod:  
`http://localhost:1880/ui`

---

## ⚙️ Konfiguracja

### Zmiana autoryzowanej karty RFID (Węzeł 1)

```cpp
// KodWezly/Wezel1/src/main.cpp
const String AUTHORIZED_UID = "BB:21:91:22";  // zamień na UID swojej karty
```

Aby odczytać UID karty, wgraj kod i obserwuj **Serial Monitor** (115200 baud) — po przyłożeniu karty zostanie wypisany jej UID w formacie `XX:XX:XX:XX`.

### Czas ważności autoryzacji (Węzeł 1)

```cpp
const unsigned long CARD_VALID_MS = 15000;  // 15 000 ms = 15 sekund
```

### Próg wykrywania wstrząsu (Węzeł 3)

```cpp
const float SHAKE_THRESHOLD = 3;  // w jednostkach G (przyspieszenie ziemskie)
```

Im niższa wartość, tym czulszy alarm. Wartość `1.0` G oznacza normalną grawitację — przy `3.0` G alarm uruchamia się tylko przy wyraźnym wstrząsie.

### Klucz szyfrowania XOR

Klucz musi być **identyczny** w kodzie ESP32 i w węźle Node-RED (Deszyfrator):

```cpp
// ESP32 (main.cpp każdego węzła)
client.publish(TOPIC, szyfrujJSON(String(buf), "INZYNIER2024").c_str());
```

```javascript
// Node-RED (węzeł Deszyfrator)
let klucz = "INZYNIER2024";
```

### Konfiguracja Telegrama (Node-RED)

1. Utwórz bota przez `@BotFather` na Telegramie i skopiuj token
2. W Node-RED edytuj węzeł **telegram bot** — wklej token
3. W węźle **Formatowanie dla API Telegram** zmień `chatId`:

```javascript
msg.payload = {
    chatId: 5066881334,  // <- zmień na swoje chat ID
    type: "message",
    content: "ALARM LODOWKA: " + msg.payload
};
```

Własne `chatId` możesz uzyskać wysyłając wiadomość do bota `@userinfobot`.
