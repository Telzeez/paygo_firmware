
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>        // ArduinoJson version 7
#include <Preferences.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ============================================================
// PINS
// ============================================================
#define PZEM_RX_PIN     16
#define PZEM_TX_PIN     17
#define I2C_SDA         21
#define I2C_SCL         22
#define RELAY_PIN       5
#define BUZZER_PIN      23
#define CONFIRM_PIN     4
#define RELAY_ON        HIGH
#define RELAY_OFF       LOW

// ============================================================
// SETTINGS
// ============================================================
const char* WIFI_SSID       = "paygo";
const char* WIFI_PASSWORD   = "paygo1234";

const char* HEARTBEAT_URL   = "https://paygo-backend-6w4q.onrender.com/api/devices/DEVICE-001/heartbeat";
const char* BALANCE_URL     = "https://paygo-backend-6w4q.onrender.com/api/devices/DEVICE-001/balance";
// const char* DEVICE_TOKEN = "change-me";   // use with the Authorization headers below

#define LCD_ADDR            0x27
#define PZEM_ADDR           0xF8
#define PZEM_BAUD           9600
#define PZEM_TRIES          3

const float VOLT_CAL = 1.000f;
const unsigned long FETCH_INTERVAL   = 5000;  // heartbeat + balance push every 10 s
const unsigned long READ_INTERVAL    = 1000;   // read PZEM every 1 s
const unsigned long SAVE_INTERVAL    = 30000;  // save to NVS at most every 30 s
const unsigned long PAGE_INTERVAL    = 3000;   // LCD page change time
const unsigned long BEEP_INTERVAL    = 5000;   // buzzer every 5 s
const unsigned long BEEP_LENGTH      = 200;    // buzzer on time

const float  LOW_KWH         = 1.0f;           // buzzer warns at or below this
const double MIN_PUSH_KWH    = 0.001;          // push once at least 5 Wh is used
const double SAME_EPS        = 0.0005;         // balances closer than this are "equal"
const double RELAY_MIN_KWH   = 0.001;          // relay cuts off below this
const uint8_t NUM_PAGES      = 2;

// ============================================================
// GLOBALS
// ============================================================
LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);
Preferences prefs;

struct PzemData {
  float voltage;
  float current;
  float power;
  float energy;
  float frequency;
  float pf;
};

enum PzemStatus {
  PZEM_OK,
  PZEM_NO_REPLY,
  PZEM_BAD_FRAME,
  PZEM_BAD_CRC
};

PzemData data = {0, 0, 0, 0, 0, 0};

// Shared between the fetch task (core 0) and loop() (core 1).
// Always touch these inside portENTER_CRITICAL(&mux) / portEXIT_CRITICAL(&mux).
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
double balanceKwh  = 0;   // last balance confirmed by the server
double pendingKwh  = 0;   // used since the last packet, not yet being pushed
double inflightKwh = 0;   // being pushed to the server, not yet confirmed
volatile bool needSave = false;

double lastPzemKwh   = 0;
bool   pzemSeeded    = false;
double remEnergy     = 0;   // available = balance - pending - inflight
bool   relayOn       = false;

unsigned long lastSave    = 0;
unsigned long lastRead    = 0;
unsigned long lastBeep    = 0;
bool          beepActive  = false;

uint8_t       page        = 0;
unsigned long lastPage    = 0;
bool          lastButton  = HIGH;
unsigned long lastPress   = 0;

// Forward declarations
void updateEnergyLogic();
void saveToNVS();
void loadFromNVS();
void setRelay(bool on);

// ============================================================
// CRC-16
// ============================================================
uint16_t crc16(const uint8_t* d, uint8_t len) {
  uint16_t crc = 0xFFFF;
  for (uint8_t i = 0; i < len; i++) {
    crc ^= d[i];
    for (uint8_t b = 0; b < 8; b++) {
      crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
    }
  }
  return crc;
}

// ============================================================
// READ PZEM
// ============================================================
PzemStatus pzemReadOnce(PzemData& out, uint8_t& bytesGot) {
  uint8_t req[8] = { PZEM_ADDR, 0x04, 0x00, 0x00, 0x00, 0x0A, 0, 0 };
  uint16_t c = crc16(req, 6);
  req[6] = c & 0xFF;
  req[7] = c >> 8;

  while (Serial2.available()) Serial2.read();
  Serial2.write(req, 8);

  uint8_t buf[25];
  uint8_t n = 0;
  unsigned long start = millis();
  while (n < 25 && millis() - start < 500) {
    if (Serial2.available()) buf[n++] = Serial2.read();
  }
  bytesGot = n;
  if (n == 0) return PZEM_NO_REPLY;
  if (n != 25 || buf[1] != 0x04 || buf[2] != 0x14) return PZEM_BAD_FRAME;

  uint16_t rc = crc16(buf, 23);
  if (buf[23] != (rc & 0xFF) || buf[24] != (rc >> 8)) return PZEM_BAD_CRC;

  auto reg = [&](uint8_t i) -> uint32_t {
    return ((uint32_t)buf[3 + 2 * i] << 8) | buf[4 + 2 * i];
  };

  out.voltage   = reg(0) / 10.0f * VOLT_CAL;
  out.current   = ((reg(2) << 16) | reg(1)) / 1000.0f;
  out.power     = ((reg(4) << 16) | reg(3)) / 10.0f;
  out.energy    = ((reg(6) << 16) | reg(5)) / 1000.0f;
  out.frequency = reg(7) / 10.0f;
  out.pf        = reg(8) / 100.0f;
  return PZEM_OK;
}

PzemStatus pzemRead(PzemData& out, uint8_t& bytesGot) {
  PzemStatus status = PZEM_NO_REPLY;
  for (uint8_t i = 0; i < PZEM_TRIES; i++) {
    status = pzemReadOnce(out, bytesGot);
    if (status == PZEM_OK) return status;
    delay(100);
  }
  return status;
}

// ============================================================
// NVS BACKUP
// ============================================================
void loadFromNVS() {
  prefs.begin("paygo", false);
  balanceKwh  = prefs.getDouble("bal2", 0.0);
  pendingKwh  = prefs.getDouble("pend2", 0.0);
  inflightKwh = prefs.getDouble("infl2", 0.0);
  if (prefs.isKey("pz")) {
    lastPzemKwh = prefs.getDouble("pz", 0.0);
    pzemSeeded  = true;
  }
}

void saveToNVS() {
  portENTER_CRITICAL(&mux);
  double b = balanceKwh;
  double p = pendingKwh;
  double i = inflightKwh;
  needSave = false;   // cleared first so a change during the write is not lost
  portEXIT_CRITICAL(&mux);

  prefs.begin("paygo", false);
  prefs.putDouble("bal2", b);
  prefs.putDouble("pend2", p);
  prefs.putDouble("infl2", i);
  prefs.putDouble("pz", lastPzemKwh);
  lastSave = millis();
}

// ============================================================
// WIFI, HEARTBEAT & BALANCE SYNC
// ============================================================

// Reads "balance" from a JSON reply. Returns true if found.
bool parseBalance(const String& payload, double& out) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) return false;
  if (!doc["balance"].is<JsonVariant>()) return false;
  out = doc["balance"].as<double>();
  if (out < 0) out = 0;
  return true;
}

// Initial Sync called at BOOT: fetches authoritative balance from the server
bool syncInitialBalance() {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(10000);
  http.begin(client, HEARTBEAT_URL);
  http.addHeader("Content-Type", "application/json");

  int code = http.POST("{}");
  Serial.printf("Initial sync HTTP code: %d\n", code);

  if (code == 200) {
    double b = 0;
    if (parseBalance(http.getString(), b)) {
      portENTER_CRITICAL(&mux);
      balanceKwh  = b;
      pendingKwh  = 0;
      inflightKwh = 0;
      portEXIT_CRITICAL(&mux);

      saveToNVS();
      Serial.printf("Initial sync SUCCESS: Server balance is %.3f kWh\n", b);
      http.end();
      return true;
    }
  } else {
    Serial.printf("Initial sync failed: %s (HTTP %d)\n", http.errorToString(code).c_str(), code);
  }
  http.end();
  return false;
}

// Marks the device ONLINE and picks up the server balance (top-ups or resets).
void sendHeartbeat() {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(10000);
  http.begin(client, HEARTBEAT_URL);
  http.addHeader("Content-Type", "application/json");

  int code = http.POST("{}");
  Serial.printf("Heartbeat HTTP code: %d\n", code);

  if (code == 200) {
    double b;
    if (parseBalance(http.getString(), b)) {
      portENTER_CRITICAL(&mux);
      // Case 1: Top-up occurred on the server (server balance increased)
      if (b > balanceKwh + SAME_EPS) {
        double topup = b - balanceKwh;
        balanceKwh = b;
        needSave = true;
        Serial.printf("Top-up detected: +%.3f kWh -> Server balance now %.3f kWh\n", topup, b);
      }
      // Case 2: Server balance was reset or updated while no local deductions are pending
      else if (inflightKwh <= 0 && pendingKwh <= 0 && fabs(balanceKwh - b) > SAME_EPS) {
        balanceKwh = b;
        needSave = true;
        Serial.printf("Server balance synced: %.3f kWh\n", b);
      }
      portEXIT_CRITICAL(&mux);
      updateEnergyLogic();
    }
  } else {
    Serial.printf("Heartbeat failed: %s (HTTP %d)\n", http.errorToString(code).c_str(), code);
  }
  http.end();
}

// Sends the remaining balance to the backend.
// Re-sent on failure until the server confirms, so no usage is lost.
void pushBalance() {
  portENTER_CRITICAL(&mux);
  if (inflightKwh <= 0 && pendingKwh >= MIN_PUSH_KWH) {
    inflightKwh = pendingKwh;
    pendingKwh  = 0;
    needSave    = true;
  }
  double inflight = inflightKwh;
  double expected = balanceKwh;
  portEXIT_CRITICAL(&mux);

  if (inflight <= 0) return;

  double target = expected - inflight;     // remaining balance to store on the server
  if (target < 0) target = 0;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(10000);
  http.begin(client, BALANCE_URL);
  http.addHeader("Content-Type", "application/json");

  JsonDocument req;
  req["balance"]  = target;     // new remaining balance
  req["expected"] = expected;   // balance the device believes is on the server

  String body;
  serializeJson(req, body);

  int code = http.PUT(body);
  Serial.printf("Balance push HTTP %d (expected %.3f -> new %.3f)\n", code, expected, target);

  if (code == 200 || code == 201 || code == 409) {
    double b = target;
    bool hasBalance = parseBalance(http.getString(), b);

    bool applied;
    if (code != 409) {
      applied = true; // Server successfully stored new balance
    } else {
      // 409 = server balance changed (top-up arrived).
      // Adopt new server balance and retry the deduction next cycle.
      applied = hasBalance && fabs(b - target) < SAME_EPS;
    }

    portENTER_CRITICAL(&mux);
    if (hasBalance || applied) balanceKwh = b;
    if (applied) inflightKwh = 0;
    needSave = true;
    portEXIT_CRITICAL(&mux);

    Serial.printf("%s. Server balance now %.3f kWh\n",
                  applied ? "Push confirmed" : "Balance changed on server (top-up), will re-send", b);
    updateEnergyLogic();
  } else {
    Serial.printf("Push failed: %s (HTTP %d, usage stays queued)\n", http.errorToString(code).c_str(), code);
  }
  http.end();
}

void fetchTask(void* param) {
  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      sendHeartbeat();
      pushBalance();
    } else {
      Serial.println("WiFi disconnected. Reconnecting...");
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
    vTaskDelay(FETCH_INTERVAL / portTICK_PERIOD_MS);
  }
}

// ============================================================
// LCD
// ============================================================
void lcdLine(uint8_t row, const char* text) {
  char line[17];
  snprintf(line, sizeof(line), "%-16s", text);
  lcd.setCursor(0, row);
  lcd.print(line);
}

void showLCD() {
  char l1[24], l2[24];
  switch (page) {
    case 0:
      snprintf(l1, sizeof(l1), "V:%.1f F:%.1fHz", data.voltage, data.frequency);
      snprintf(l2, sizeof(l2), "I:%.2fA P:%.0fW", data.current, data.power);
      break;
    default:
      snprintf(l1, sizeof(l1), "Available:");
      snprintf(l2, sizeof(l2), "%.3f kWh %s", (float)remEnergy, relayOn ? "ON" : "OFF");
      break;
  }
  lcdLine(0, l1);
  lcdLine(1, l2);
}

// ============================================================
// RELAY AND BUZZER
// ============================================================
void setRelay(bool on) {
  relayOn = on;
  digitalWrite(RELAY_PIN, on ? RELAY_ON : RELAY_OFF);
}

void updateEnergyLogic() {
  // Available = server balance minus pending and in-flight local consumption
  portENTER_CRITICAL(&mux);
  double avail = balanceKwh - pendingKwh - inflightKwh;
  portEXIT_CRITICAL(&mux);

  remEnergy = (avail < 0) ? 0 : avail;

  bool shouldBeOn = (remEnergy > RELAY_MIN_KWH);
  if (relayOn && !shouldBeOn) {
    saveToNVS();   // save right when the load is cut off
  }
  setRelay(shouldBeOn);
}

void handleBuzzer() {
  unsigned long now = millis();
  bool low = (remEnergy > 0 && remEnergy <= LOW_KWH);

  if (low && !beepActive && now - lastBeep >= BEEP_INTERVAL) {
    lastBeep   = now;
    beepActive = true;
    digitalWrite(BUZZER_PIN, HIGH);
  }
  if (beepActive && now - lastBeep >= BEEP_LENGTH) {
    beepActive = false;
    digitalWrite(BUZZER_PIN, LOW);
  }
  if (!low && beepActive) {
    beepActive = false;
    digitalWrite(BUZZER_PIN, LOW);
  }
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  setRelay(false);                       // load stays OFF until balance is verified
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(CONFIRM_PIN, INPUT_PULLUP);

  Wire.begin(I2C_SDA, I2C_SCL);
  lcd.init();
  lcd.backlight();
  lcdLine(0, "PAYGO SOLAR");
  lcdLine(1, "Connecting WiFi.");

  Serial2.begin(PZEM_BAUD, SERIAL_8N1, PZEM_RX_PIN, PZEM_TX_PIN);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  // Wait up to 10 seconds for WiFi connection
  unsigned long startWifi = millis();
  int dots = 0;
  while (WiFi.status() != WL_CONNECTED && millis() - startWifi < 10000) {
    delay(500);
    dots = (dots + 1) % 4;
    char wLine[17];
    snprintf(wLine, sizeof(wLine), "Connecting WiFi%.*s", dots, "...");
    lcdLine(1, wLine);
    Serial.print(".");
  }
  Serial.println();

  bool synced = false;
  if (WiFi.status() == WL_CONNECTED) {
    lcdLine(1, "Syncing Server..");
    synced = syncInitialBalance();
  }

  if (synced) {
    lcdLine(1, "Sync Complete!  ");
    delay(1000);
  } else {
    // If offline on boot, fallback to last known NVS balance
    loadFromNVS();
    lcdLine(1, "Offline Mode    ");
    delay(1500);
  }

  // Seed PZEM baseline reading
  uint8_t got = 0;
  PzemStatus pzStatus = pzemRead(data, got);
  if (pzStatus == PZEM_OK) {
    lastPzemKwh = data.energy;
    pzemSeeded  = true;
  }

  updateEnergyLogic();
  showLCD();

  // Start background fetch task (Heartbeat & Balance Push on Core 0)
  xTaskCreatePinnedToCore(fetchTask, "fetch", 12288, NULL, 1, NULL, 0);
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  unsigned long now = millis();

  if (now - lastRead >= READ_INTERVAL) {
    lastRead = now;
    uint8_t got = 0;
    PzemStatus status = pzemRead(data, got);

    if (status == PZEM_OK) {
      if (!pzemSeeded) {
        lastPzemKwh = data.energy;       // first ever run, start counting from here
        pzemSeeded  = true;
        needSave    = true;
      } else {
        double d = data.energy - lastPzemKwh;
        if (d < 0) d = data.energy;      // PZEM counter was reset
        if (d > 0) {
          portENTER_CRITICAL(&mux);
          pendingKwh += d;               // deduct locally right away
          portEXIT_CRITICAL(&mux);
          needSave = true;
        }
        lastPzemKwh = data.energy;
      }

      updateEnergyLogic();

      portENTER_CRITICAL(&mux);
      double b = balanceKwh, p = pendingKwh, f = inflightKwh;
      portEXIT_CRITICAL(&mux);

      Serial.printf(
        "V: %.1f | I: %.3f A | P: %.1f W | PZEM: %.3f kWh | "
        "Srv: %.3f | Pend: %.4f | Infl: %.4f | Avail: %.3f | Relay: %s\n",
        data.voltage, data.current, data.power, data.energy,
        b, p, f, (float)remEnergy, relayOn ? "ON" : "OFF");
    } else {
      switch (status) {
        case PZEM_NO_REPLY:  Serial.println("PZEM: No reply. Check power and RX/TX."); break;
        case PZEM_BAD_FRAME: Serial.printf("PZEM: Bad frame (%u bytes)\n", got);       break;
        case PZEM_BAD_CRC:   Serial.println("PZEM: CRC mismatch.");                    break;
        default: break;
      }
      updateEnergyLogic();   // balance may have changed even if PZEM failed
    }
    showLCD();
  }

  if (needSave && now - lastSave >= SAVE_INTERVAL) {
    saveToNVS();
  }

  bool btn = digitalRead(CONFIRM_PIN);
  if (btn == LOW && lastButton == HIGH && now - lastPress > 200) {
    lastPress = now;
    page = (page + 1) % NUM_PAGES;
    lastPage = now;
    showLCD();
  }
  lastButton = btn;

  if (now - lastPage >= PAGE_INTERVAL) {
    lastPage = now;
    page = (page + 1) % NUM_PAGES;
    showLCD();
  }

  handleBuzzer();
}