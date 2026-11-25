/*
 * PAYGO ESP32 - Standalone Arduino IDE sketch
 * Consolidated from the current multi-file PAYGO firmware source.
 * Required libraries: ArduinoJson 7.x; LiquidCrystal_I2C (Frank de Brabander).
 * Board package: esp32 by Espressif Systems.
 *
 * Configure device ID, unique API key, Wi-Fi credentials and verified TLS
 * root CA below before compiling. Do not disable TLS verification or commit
 * real device credentials. Communication remains HTTPS, not MQTT.
 */
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/portmacro.h>
#include <cmath>
#include <cstring>
#include <ctime>

// Device-specific configuration. Replace placeholders before uploading.
#define PAYGO_DEVICE_ID "YOUR_REGISTERED_DEVICE_ID"
#define PAYGO_API_BASE_URL "https://paygo-backend-6w4q.onrender.com"
#define PAYGO_DEVICE_API_KEY "PASTE_UNIQUE_DEVICE_API_KEY_HERE"
#define PAYGO_WIFI_SSID "YOUR_WIFI_SSID"
#define PAYGO_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define PAYGO_TLS_CA_CONFIGURED 0
static const char PAYGO_TLS_ROOT_CA_PEM[] PROGMEM = R"PAYGO_CA(
-----BEGIN CERTIFICATE-----
REPLACE_WITH_VERIFIED_ROOT_CA_CERTIFICATE
-----END CERTIFICATE-----
)PAYGO_CA";

#if PAYGO_TLS_CA_CONFIGURED != 1
#error "Configure and verify PAYGO_TLS_ROOT_CA_PEM, then set PAYGO_TLS_CA_CONFIGURED to 1. TLS verification must remain enabled."
#endif



namespace HardwareConfig {
constexpr uint8_t PZEM_RX_PIN = 16;
constexpr uint8_t PZEM_TX_PIN = 17;
constexpr uint8_t I2C_SDA_PIN = 21;
constexpr uint8_t I2C_SCL_PIN = 22;
constexpr uint8_t RELAY_PIN = 5;
constexpr uint8_t BUZZER_PIN = 23;
constexpr uint8_t CONFIRM_BUTTON_PIN = 4;

constexpr uint8_t RELAY_ON_LEVEL = HIGH;
constexpr uint8_t RELAY_OFF_LEVEL = LOW;

constexpr uint8_t LCD_ADDRESS = 0x27;
constexpr uint8_t LCD_COLUMNS = 12;
constexpr uint8_t LCD_ROWS = 2;
constexpr uint8_t PZEM_ADDRESS = 0xF8;
constexpr uint32_t PZEM_BAUD_RATE = 9600;
constexpr uint8_t PZEM_READ_ATTEMPTS = 3;
constexpr float VOLTAGE_CALIBRATION = 1.0f;
}  // namespace HardwareConfig

namespace AppConfig {
constexpr uint32_t FETCH_INTERVAL_MS = 5000;
constexpr uint32_t METER_READ_INTERVAL_MS = 1000;
constexpr uint32_t STORAGE_INTERVAL_MS = 30000;
constexpr uint32_t DISPLAY_PAGE_INTERVAL_MS = 3000;
constexpr uint32_t BUZZER_INTERVAL_MS = 5000;
constexpr uint32_t BUZZER_DURATION_MS = 200;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 10000;
constexpr uint32_t HTTP_TIMEOUT_MS = 10000;
constexpr uint32_t BACKEND_ONLINE_TTL_MS = 15000;

constexpr double MIN_PUSH_KWH = 0.001;
constexpr double BALANCE_EQUALITY_EPSILON_KWH = 0.0005;
constexpr double RELAY_MINIMUM_KWH = 0.001;
constexpr double LOW_BALANCE_WARNING_KWH = 1.0;
constexpr uint8_t DISPLAY_PAGE_COUNT = 3;
}  // namespace AppConfig


// Shared data types


struct PzemData {
  float voltage = 0;
  float current = 0;
  float power = 0;
  float energy = 0;
  float frequency = 0;
  float powerFactor = 0;
};

enum class PzemStatus {
  OK,
  NO_REPLY,
  BAD_FRAME,
  BAD_CRC
};

struct EnergySnapshot {
  double balanceKwh = 0;
  double pendingKwh = 0;
  double inflightKwh = 0;
  double lastPzemKwh = 0;
  bool pzemSeeded = false;
  uint8_t assignmentState = 0;
};

struct BalanceReply {
  int httpStatus = 0;
  bool hasBalance = false;
  double balanceKwh = 0;
  bool resetBalance = false;
  String resetCommandId;
  String assignmentStatus;
  String error;
};


// Component declarations


class BalanceStore {
 public:
  bool begin();
  bool load(EnergySnapshot& snapshot);
  bool save(const EnergySnapshot& snapshot);

 private:
  Preferences preferences_;
  bool ready_ = false;
};


class FrontPanel {
 public:
  void begin();
  void showMessage(const char* line1, const char* line2);
  void render(const PzemData& meter, double availableKwh, bool meterValid,
              bool backendOnline, uint8_t assignmentState);
  bool updatePage(uint32_t now);

 private:
  void writeLine(uint8_t row, const char* text);
  void formatDeviceLabel(char* output, size_t outputSize) const;

  LiquidCrystal_I2C lcd_{HardwareConfig::LCD_ADDRESS,
                         HardwareConfig::LCD_COLUMNS,
                         HardwareConfig::LCD_ROWS};
  uint8_t page_ = 0;
  uint32_t lastPageAt_ = 0;
  uint32_t lastButtonAt_ = 0;
  bool lastButtonState_ = HIGH;
  bool lineDrawn_[2] = {false, false};
  char lastLines_[2][HardwareConfig::LCD_COLUMNS + 1] = {{0}, {0}};
};


class LoadRelay {
 public:
  void begin();
  void setOn(bool on);
  bool isOn() const;

 private:
  bool isOn_ = false;
};


class LowBalanceBuzzer {
 public:
  void begin();
  void update(double availableKwh, uint32_t now);

 private:
  uint32_t lastBeepAt_ = 0;
  bool active_ = false;
};


class PayGoApi {
 public:
  void beginWifi();
  bool isConnected() const;
  void reconnectWifi();
  bool waitForSystemTime(uint32_t timeoutMs);
  BalanceReply postHeartbeat();
  BalanceReply putBalance(double balanceKwh, double expectedKwh);

 private:
  BalanceReply sendRequest(const char* method, const String& url, const String& body);
  static bool parseBalance(const String& body, double& balanceKwh);
  String deviceEndpoint(const char* suffix) const;
  String pendingResetAckId_;
};


class PzemMeter {
 public:
  void begin();
  PzemStatus read(PzemData& output, uint8_t& bytesReceived);

 private:
  static uint16_t crc16(const uint8_t* data, uint8_t length);
  PzemStatus readOnce(PzemData& output, uint8_t& bytesReceived);
};



class PayGoApp {
 public:
  void setup();
  void loop();

 private:
  static void fetchTaskEntry(void* parameter);
  void fetchTask();
  bool syncInitialBalance();
  void sendHeartbeat();
  void pushBalance();
  void updateEnergyLogic();
  void applyOutputState();
  void saveToStorage();
  EnergySnapshot snapshot();
  double availableEnergyKwh();
  bool backendOnline() const;
  void logMeterFailure(PzemStatus status, uint8_t bytesReceived);

  PzemMeter meter_;
  BalanceStore store_;
  PayGoApi api_;
  FrontPanel panel_;
  LoadRelay relay_;
  LowBalanceBuzzer buzzer_;

  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  double balanceKwh_ = 0;
  double pendingKwh_ = 0;
  double inflightKwh_ = 0;
  double availableKwh_ = 0;
  bool relayDesiredOn_ = false;
  volatile bool needSave_ = false;

  PzemData meterData_;
  double lastPzemKwh_ = 0;
  bool pzemSeeded_ = false;
  bool meterValid_ = false;
  volatile uint8_t assignmentState_ = 0;
  volatile uint32_t lastSuccessfulHeartbeatAt_ = 0;
  uint32_t lastStorageAt_ = 0;
  uint32_t lastMeterReadAt_ = 0;
};


// Component implementations


bool BalanceStore::begin() {
  ready_ = preferences_.begin("paygo", false);
  return ready_;
}

bool BalanceStore::load(EnergySnapshot& snapshot) {
  if (!ready_) {
    return false;
  }

  snapshot.balanceKwh = preferences_.getDouble("bal2", 0.0);
  snapshot.pendingKwh = preferences_.getDouble("pend2", 0.0);
  snapshot.inflightKwh = preferences_.getDouble("infl2", 0.0);
  snapshot.pzemSeeded = preferences_.isKey("pz");
  if (snapshot.pzemSeeded) {
    snapshot.lastPzemKwh = preferences_.getDouble("pz", 0.0);
  }
  snapshot.assignmentState = preferences_.getUChar("ast", 0);
  return true;
}

bool BalanceStore::save(const EnergySnapshot& snapshot) {
  if (!ready_) {
    return false;
  }

  const size_t balanceBytes = preferences_.putDouble("bal2", snapshot.balanceKwh);
  const size_t pendingBytes = preferences_.putDouble("pend2", snapshot.pendingKwh);
  const size_t inflightBytes = preferences_.putDouble("infl2", snapshot.inflightKwh);
  const size_t pzemBytes = preferences_.putDouble("pz", snapshot.lastPzemKwh);
  const size_t activationBytes = preferences_.putUChar("ast", snapshot.assignmentState);
  return balanceBytes > 0 && pendingBytes > 0 && inflightBytes > 0 &&
         pzemBytes > 0 && activationBytes > 0;
}




void FrontPanel::begin() {
  Wire.begin(HardwareConfig::I2C_SDA_PIN, HardwareConfig::I2C_SCL_PIN);
  lcd_.init();
  lcd_.backlight();
  pinMode(HardwareConfig::CONFIRM_BUTTON_PIN, INPUT_PULLUP);
}

void FrontPanel::showMessage(const char* line1, const char* line2) {
  writeLine(0, line1);
  writeLine(1, line2);
}

void FrontPanel::render(const PzemData& meter, double availableKwh, bool meterValid,
                        bool backendOnline, uint8_t assignmentState) {
  char line1[32] = {0};
  char line2[32] = {0};

  if (page_ == 0) {
    if (meterValid) {
      snprintf(line1, sizeof(line1), "V:%.0f F:%.0fHz", meter.voltage, meter.frequency);
      if (meter.current < 10.0f) {
        snprintf(line2, sizeof(line2), "I:%.1fA P%.0fW", meter.current, meter.power);
      } else {
        snprintf(line2, sizeof(line2), "I:%.0fA P%.0fW", meter.current, meter.power);
      }
    } else {
      snprintf(line1, sizeof(line1), "V:--- F:--Hz");
      snprintf(line2, sizeof(line2), "I:--- P:---W");
    }
  } else if (page_ == 1) {
    if (meterValid) {
      snprintf(line1, sizeof(line1), "E:%.2fkWh", meter.energy);
    } else {
      snprintf(line1, sizeof(line1), "E:--");
    }
    snprintf(line2, sizeof(line2), "B:%.2fkWh", availableKwh);
  } else {
    formatDeviceLabel(line1, sizeof(line1));
    const char* assignmentLabel = "UNAS";
    switch (assignmentState) {
      case 1: assignmentLabel = "ASGN"; break;
      case 2: assignmentLabel = "ACTV"; break;
      case 3: assignmentLabel = "SUSP"; break;
      default: break;
    }
    snprintf(line2, sizeof(line2), "%s|%s",
             backendOnline ? "ONLINE" : "OFFLINE", assignmentLabel);
  }

  writeLine(0, line1);
  writeLine(1, line2);
}

void FrontPanel::formatDeviceLabel(char* output, size_t outputSize) const {
  const char* deviceId = PAYGO_DEVICE_ID;
  size_t end = strlen(deviceId);
  size_t start = end;

  while (start > 0 && deviceId[start - 1] >= '0' && deviceId[start - 1] <= '9') {
    --start;
  }

  char suffix[5] = "0001";
  const size_t digitCount = end - start;
  if (digitCount > 0) {
    const size_t copyCount = digitCount > 4 ? 4 : digitCount;
    memset(suffix, '0', 4);
    memcpy(suffix + (4 - copyCount), deviceId + end - copyCount, copyCount);
    suffix[4] = '\0';
  } else {
    snprintf(suffix, sizeof(suffix), "----");
  }

  snprintf(output, outputSize, "PAYGO #%s", suffix);
}

bool FrontPanel::updatePage(uint32_t now) {
  bool changed = false;
  const bool buttonState = digitalRead(HardwareConfig::CONFIRM_BUTTON_PIN);
  if (buttonState == LOW && lastButtonState_ == HIGH && now - lastButtonAt_ > 200) {
    lastButtonAt_ = now;
    page_ = (page_ + 1) % AppConfig::DISPLAY_PAGE_COUNT;
    lastPageAt_ = now;
    changed = true;
  }
  lastButtonState_ = buttonState;

  if (now - lastPageAt_ >= AppConfig::DISPLAY_PAGE_INTERVAL_MS) {
    lastPageAt_ = now;
    page_ = (page_ + 1) % AppConfig::DISPLAY_PAGE_COUNT;
    changed = true;
  }
  return changed;
}

void FrontPanel::writeLine(uint8_t row, const char* text) {
  if (row >= HardwareConfig::LCD_ROWS) {
    return;
  }

  char line[HardwareConfig::LCD_COLUMNS + 1];
  snprintf(line, sizeof(line), "%-*.*s",
           static_cast<int>(HardwareConfig::LCD_COLUMNS),
           static_cast<int>(HardwareConfig::LCD_COLUMNS),
           text ? text : "");

  if (lineDrawn_[row] && strcmp(lastLines_[row], line) == 0) {
    return;
  }

  lcd_.setCursor(0, row);
  lcd_.print(line);
  memcpy(lastLines_[row], line, sizeof(line));
  lineDrawn_[row] = true;
}



void LoadRelay::begin() {
  pinMode(HardwareConfig::RELAY_PIN, OUTPUT);
  setOn(false);
}

void LoadRelay::setOn(bool on) {
  isOn_ = on;
  digitalWrite(HardwareConfig::RELAY_PIN,
               on ? HardwareConfig::RELAY_ON_LEVEL : HardwareConfig::RELAY_OFF_LEVEL);
}

bool LoadRelay::isOn() const {
  return isOn_;
}



void LowBalanceBuzzer::begin() {
  pinMode(HardwareConfig::BUZZER_PIN, OUTPUT);
  digitalWrite(HardwareConfig::BUZZER_PIN, LOW);
}

void LowBalanceBuzzer::update(double availableKwh, uint32_t now) {
  const bool lowBalance = availableKwh > 0 &&
                          availableKwh <= AppConfig::LOW_BALANCE_WARNING_KWH;

  if (lowBalance && !active_ && now - lastBeepAt_ >= AppConfig::BUZZER_INTERVAL_MS) {
    lastBeepAt_ = now;
    active_ = true;
    digitalWrite(HardwareConfig::BUZZER_PIN, HIGH);
  }
  if (active_ && now - lastBeepAt_ >= AppConfig::BUZZER_DURATION_MS) {
    active_ = false;
    digitalWrite(HardwareConfig::BUZZER_PIN, LOW);
  }
  if (!lowBalance && active_) {
    active_ = false;
    digitalWrite(HardwareConfig::BUZZER_PIN, LOW);
  }
}




void PayGoApi::beginWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(PAYGO_WIFI_SSID, PAYGO_WIFI_PASSWORD);
}

bool PayGoApi::isConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

void PayGoApi::reconnectWifi() {
  WiFi.disconnect();
  WiFi.begin(PAYGO_WIFI_SSID, PAYGO_WIFI_PASSWORD);
}

bool PayGoApi::waitForSystemTime(uint32_t timeoutMs) {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  const unsigned long startedAt = millis();
  while (time(nullptr) < 1700000000 && millis() - startedAt < timeoutMs) {
    delay(100);
  }
  return time(nullptr) >= 1700000000;
}

String PayGoApi::deviceEndpoint(const char* suffix) const {
  return String(PAYGO_API_BASE_URL) + "/api/devices/" + PAYGO_DEVICE_ID + suffix;
}

BalanceReply PayGoApi::postHeartbeat() {
  JsonDocument request;
  if (pendingResetAckId_.length() > 0) {
    request["ackBalanceReset"] = pendingResetAckId_;
  }
  String body;
  serializeJson(request, body);

  const bool sentResetAck = pendingResetAckId_.length() > 0;
  BalanceReply reply = sendRequest("POST", deviceEndpoint("/heartbeat"), body);
  if (reply.httpStatus == 200) {
    if (sentResetAck) {
      pendingResetAckId_ = "";
    }
    if (reply.resetBalance && reply.resetCommandId.length() > 0) {
      pendingResetAckId_ = reply.resetCommandId;
    }
  }
  return reply;
}

BalanceReply PayGoApi::putBalance(double balanceKwh, double expectedKwh) {
  JsonDocument request;
  request["balance"] = balanceKwh;
  request["expected"] = expectedKwh;
  String body;
  serializeJson(request, body);
  return sendRequest("PUT", deviceEndpoint("/balance"), body);
}

BalanceReply PayGoApi::sendRequest(const char* method, const String& url, const String& body) {
  BalanceReply result;
  if (!isConnected()) {
    result.error = "Wi-Fi is not connected";
    return result;
  }
  if (time(nullptr) < 1700000000 &&
      !waitForSystemTime(AppConfig::WIFI_CONNECT_TIMEOUT_MS)) {
    result.error = "System time unavailable for HTTPS certificate validation";
    return result;
  }

  WiFiClientSecure client;
  client.setCACert(PAYGO_TLS_ROOT_CA_PEM);

  HTTPClient http;
  http.setTimeout(AppConfig::HTTP_TIMEOUT_MS);
  if (!http.begin(client, url)) {
    result.error = "Could not initialize HTTPS request";
    return result;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Device-API-Key", PAYGO_DEVICE_API_KEY);

  if (strcmp(method, "POST") == 0) {
    result.httpStatus = http.POST(body);
  } else {
    result.httpStatus = http.PUT(body);
  }

  if (result.httpStatus > 0) {
    const String responseBody = http.getString();
    result.hasBalance = parseBalance(responseBody, result.balanceKwh);
    JsonDocument response;
    if (!deserializeJson(response, responseBody)) {
      result.resetBalance = response["resetBalance"] | false;
      result.resetCommandId = response["resetCommandId"] | "";
      result.assignmentStatus = response["assignmentStatus"] | "";
    }
  } else {
    result.error = http.errorToString(result.httpStatus);
  }
  http.end();
  return result;
}

bool PayGoApi::parseBalance(const String& body, double& balanceKwh) {
  JsonDocument response;
  if (deserializeJson(response, body)) {
    return false;
  }

  const JsonVariantConst value = response["balance"];
  if (!value.is<double>()) {
    return false;
  }

  const double parsedBalance = value.as<double>();
  if (!std::isfinite(parsedBalance) || parsedBalance < 0) {
    return false;
  }
  balanceKwh = parsedBalance;
  return true;
}



void PzemMeter::begin() {
  Serial2.begin(
      HardwareConfig::PZEM_BAUD_RATE,
      SERIAL_8N1,
      HardwareConfig::PZEM_RX_PIN,
      HardwareConfig::PZEM_TX_PIN);
}

uint16_t PzemMeter::crc16(const uint8_t* data, uint8_t length) {
  uint16_t crc = 0xFFFF;
  for (uint8_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
    }
  }
  return crc;
}

PzemStatus PzemMeter::readOnce(PzemData& output, uint8_t& bytesReceived) {
  uint8_t request[8] = {
      HardwareConfig::PZEM_ADDRESS, 0x04, 0x00, 0x00, 0x00, 0x0A, 0, 0};
  const uint16_t requestCrc = crc16(request, 6);
  request[6] = requestCrc & 0xFF;
  request[7] = requestCrc >> 8;

  while (Serial2.available()) {
    Serial2.read();
  }
  Serial2.write(request, sizeof(request));

  uint8_t response[25];
  uint8_t received = 0;
  const unsigned long startedAt = millis();
  while (received < sizeof(response) && millis() - startedAt < 500) {
    if (Serial2.available()) {
      response[received++] = Serial2.read();
    }
  }

  bytesReceived = received;
  if (received == 0) {
    return PzemStatus::NO_REPLY;
  }
  if (received != sizeof(response) || response[1] != 0x04 || response[2] != 0x14) {
    return PzemStatus::BAD_FRAME;
  }

  const uint16_t responseCrc = crc16(response, 23);
  if (response[23] != (responseCrc & 0xFF) || response[24] != (responseCrc >> 8)) {
    return PzemStatus::BAD_CRC;
  }

  const auto registerValue = [&response](uint8_t index) -> uint32_t {
    return (static_cast<uint32_t>(response[3 + 2 * index]) << 8) |
           response[4 + 2 * index];
  };

  output.voltage = registerValue(0) / 10.0f * HardwareConfig::VOLTAGE_CALIBRATION;
  output.current = ((registerValue(2) << 16) | registerValue(1)) / 1000.0f;
  output.power = ((registerValue(4) << 16) | registerValue(3)) / 10.0f;
  output.energy = ((registerValue(6) << 16) | registerValue(5)) / 1000.0f;
  output.frequency = registerValue(7) / 10.0f;
  output.powerFactor = registerValue(8) / 100.0f;
  return PzemStatus::OK;
}

PzemStatus PzemMeter::read(PzemData& output, uint8_t& bytesReceived) {
  PzemStatus status = PzemStatus::NO_REPLY;
  for (uint8_t attempt = 0; attempt < HardwareConfig::PZEM_READ_ATTEMPTS; ++attempt) {
    status = readOnce(output, bytesReceived);
    if (status == PzemStatus::OK) {
      return status;
    }
    delay(100);
  }
  return status;
}




namespace {
uint8_t assignmentStateFromApi(const String& status) {
  if (status == "ASSIGNED") return 1;
  if (status == "ACTIVATED") return 2;
  if (status == "SUSPENDED") return 3;
  return 0;
}
}  // namespace

void PayGoApp::setup() {
  Serial.begin(115200);
  relay_.begin();
  buzzer_.begin();
  panel_.begin();
  panel_.showMessage("PAYGO SOLAR", "Connecting WiFi.");
  meter_.begin();

  if (!store_.begin()) {
    Serial.println("Could not initialize device storage.");
  }

  api_.beginWifi();
  const uint32_t wifiStartedAt = millis();
  uint8_t dots = 0;
  while (!api_.isConnected() &&
         millis() - wifiStartedAt < AppConfig::WIFI_CONNECT_TIMEOUT_MS) {
    delay(500);
    dots = (dots + 1) % 4;
    char line[17];
    snprintf(line, sizeof(line), "Connecting WiFi%.*s", dots, "...");
    panel_.showMessage("PAYGO SOLAR", line);
    Serial.print(".");
  }
  Serial.println();

  bool synced = false;
  if (api_.isConnected()) {
    panel_.showMessage("PAYGO SOLAR", "Syncing Server..");
    synced = syncInitialBalance();
  }

  if (synced) {
    panel_.showMessage("PAYGO SOLAR", "Sync Complete!");
    delay(1000);
  } else {
    EnergySnapshot saved;
    if (store_.load(saved)) {
      portENTER_CRITICAL(&mux_);
      balanceKwh_ = saved.balanceKwh;
      pendingKwh_ = saved.pendingKwh;
      inflightKwh_ = saved.inflightKwh;
      portEXIT_CRITICAL(&mux_);
      lastPzemKwh_ = saved.lastPzemKwh;
      pzemSeeded_ = saved.pzemSeeded;
      assignmentState_ = saved.assignmentState;
    }
    panel_.showMessage("PAYGO SOLAR", "Offline Mode");
    delay(1500);
  }

  uint8_t bytesReceived = 0;
  if (meter_.read(meterData_, bytesReceived) == PzemStatus::OK) {
    meterValid_ = true;
    lastPzemKwh_ = meterData_.energy;
    pzemSeeded_ = true;
  } else {
    meterValid_ = false;
  }

  updateEnergyLogic();
  applyOutputState();
  panel_.render(meterData_, availableEnergyKwh(), meterValid_, backendOnline(), assignmentState_);
  lastStorageAt_ = millis();

  const BaseType_t taskCreated = xTaskCreatePinnedToCore(
      fetchTaskEntry, "fetch", 12288, this, 1, nullptr, 0);
  if (taskCreated != pdPASS) {
    Serial.println("Could not start the network synchronization task.");
  }
}

void PayGoApp::loop() {
  const uint32_t now = millis();
  bool renderPanel = false;

  if (now - lastMeterReadAt_ >= AppConfig::METER_READ_INTERVAL_MS) {
    lastMeterReadAt_ = now;
    uint8_t bytesReceived = 0;
    const PzemStatus status = meter_.read(meterData_, bytesReceived);

    if (status == PzemStatus::OK) {
      meterValid_ = true;
      if (!pzemSeeded_) {
        lastPzemKwh_ = meterData_.energy;
        pzemSeeded_ = true;
        needSave_ = true;
      } else {
        double consumedKwh = meterData_.energy - lastPzemKwh_;
        if (consumedKwh < 0) {
          consumedKwh = meterData_.energy;
        }
        if (consumedKwh > 0) {
          portENTER_CRITICAL(&mux_);
          pendingKwh_ += consumedKwh;
          portEXIT_CRITICAL(&mux_);
          needSave_ = true;
        }
        lastPzemKwh_ = meterData_.energy;
      }

      updateEnergyLogic();
      applyOutputState();
      const EnergySnapshot current = snapshot();
      Serial.printf(
          "V: %.1f | I: %.3f A | P: %.1f W | PZEM: %.3f kWh | "
          "Srv: %.3f | Pend: %.4f | Infl: %.4f | Avail: %.3f | Relay: %s\n",
          meterData_.voltage, meterData_.current, meterData_.power, meterData_.energy,
          current.balanceKwh, current.pendingKwh, current.inflightKwh,
          static_cast<float>(availableEnergyKwh()), relay_.isOn() ? "ON" : "OFF");
    } else {
      meterValid_ = false;
      logMeterFailure(status, bytesReceived);
      updateEnergyLogic();
      applyOutputState();
    }
    renderPanel = true;
  }

  if (needSave_ && now - lastStorageAt_ >= AppConfig::STORAGE_INTERVAL_MS) {
    saveToStorage();
  }

  applyOutputState();
  if (panel_.updatePage(now)) {
    renderPanel = true;
  }
  if (renderPanel) {
    panel_.render(meterData_, availableEnergyKwh(), meterValid_, backendOnline(), assignmentState_);
  }
  buzzer_.update(availableEnergyKwh(), now);
}

void PayGoApp::fetchTaskEntry(void* parameter) {
  static_cast<PayGoApp*>(parameter)->fetchTask();
}

void PayGoApp::fetchTask() {
  for (;;) {
    if (api_.isConnected()) {
      sendHeartbeat();
      pushBalance();
    } else {
      Serial.println("WiFi disconnected. Reconnecting...");
      api_.reconnectWifi();
    }
    vTaskDelay(pdMS_TO_TICKS(AppConfig::FETCH_INTERVAL_MS));
  }
}

bool PayGoApp::syncInitialBalance() {
  if (!api_.isConnected()) {
    return false;
  }
  if (!api_.waitForSystemTime(AppConfig::WIFI_CONNECT_TIMEOUT_MS)) {
    Serial.println("System time unavailable; HTTPS certificate validation cannot proceed.");
    return false;
  }

  const BalanceReply reply = api_.postHeartbeat();
  Serial.printf("Initial sync HTTP code: %d\n", reply.httpStatus);
  if (reply.httpStatus != 200 || !reply.hasBalance) {
    if (reply.error.length() > 0) {
      Serial.printf("Initial sync failed: %s\n", reply.error.c_str());
    }
    return false;
  }

  lastSuccessfulHeartbeatAt_ = millis();
  if (reply.assignmentStatus.length() > 0) {
    assignmentState_ = assignmentStateFromApi(reply.assignmentStatus);
  }

  portENTER_CRITICAL(&mux_);
  balanceKwh_ = reply.balanceKwh;
  pendingKwh_ = 0;
  inflightKwh_ = 0;
  portEXIT_CRITICAL(&mux_);

  saveToStorage();
  Serial.printf("Initial sync SUCCESS: Server balance is %.3f kWh\n", reply.balanceKwh);
  return true;
}

void PayGoApp::sendHeartbeat() {
  const BalanceReply reply = api_.postHeartbeat();
  Serial.printf("Heartbeat HTTP code: %d\n", reply.httpStatus);

  if (reply.httpStatus == 200 && reply.hasBalance) {
    lastSuccessfulHeartbeatAt_ = millis();
    if (reply.assignmentStatus.length() > 0) {
      const uint8_t assignmentState = assignmentStateFromApi(reply.assignmentStatus);
      if (assignmentState_ != assignmentState) {
        assignmentState_ = assignmentState;
        needSave_ = true;
      }
    }

    if (reply.resetBalance) {
      portENTER_CRITICAL(&mux_);
      balanceKwh_ = 0;
      pendingKwh_ = 0;
      inflightKwh_ = 0;
      needSave_ = true;
      portEXIT_CRITICAL(&mux_);
      updateEnergyLogic();
      saveToStorage();
      Serial.printf("Owner balance reset command %s applied. Local balance is now 0 kWh\n",
                    reply.resetCommandId.c_str());
      return;
    }

    bool topupDetected = false;
    double topupKwh = 0;
    bool balanceChanged = false;

    portENTER_CRITICAL(&mux_);
    if (reply.balanceKwh > balanceKwh_ + AppConfig::BALANCE_EQUALITY_EPSILON_KWH) {
      topupKwh = reply.balanceKwh - balanceKwh_;
      balanceKwh_ = reply.balanceKwh;
      needSave_ = true;
      topupDetected = true;
    } else if (inflightKwh_ <= 0 && pendingKwh_ <= 0 &&
               fabs(balanceKwh_ - reply.balanceKwh) >
                   AppConfig::BALANCE_EQUALITY_EPSILON_KWH) {
      balanceKwh_ = reply.balanceKwh;
      needSave_ = true;
      balanceChanged = true;
    }
    portEXIT_CRITICAL(&mux_);

    if (topupDetected) {
      Serial.printf("Top-up detected: +%.3f kWh -> Server balance now %.3f kWh\n",
                    topupKwh, reply.balanceKwh);
    } else if (balanceChanged) {
      Serial.printf("Server balance synced: %.3f kWh\n", reply.balanceKwh);
    }
    updateEnergyLogic();
  } else if (reply.error.length() > 0) {
    Serial.printf("Heartbeat failed: %s (HTTP %d)\n",
                  reply.error.c_str(), reply.httpStatus);
  }
}

void PayGoApp::pushBalance() {
  portENTER_CRITICAL(&mux_);
  if (inflightKwh_ <= 0 && pendingKwh_ >= AppConfig::MIN_PUSH_KWH) {
    inflightKwh_ = pendingKwh_;
    pendingKwh_ = 0;
    needSave_ = true;
  }
  const double inflight = inflightKwh_;
  const double expected = balanceKwh_;
  portEXIT_CRITICAL(&mux_);

  if (inflight <= 0) {
    return;
  }

  double target = expected - inflight;
  if (target < 0) {
    target = 0;
  }

  const BalanceReply reply = api_.putBalance(target, expected);
  Serial.printf("Balance push HTTP %d (expected %.3f -> new %.3f)\n",
                reply.httpStatus, expected, target);

  if (reply.httpStatus == 200 || reply.httpStatus == 201 || reply.httpStatus == 409) {
    const double serverBalance = reply.hasBalance ? reply.balanceKwh : target;
    const bool applied = reply.httpStatus != 409 ||
                         (reply.hasBalance &&
                          fabs(serverBalance - target) <
                              AppConfig::BALANCE_EQUALITY_EPSILON_KWH);

    portENTER_CRITICAL(&mux_);
    if (reply.hasBalance || applied) {
      balanceKwh_ = serverBalance;
    }
    if (applied) {
      inflightKwh_ = 0;
    }
    needSave_ = true;
    portEXIT_CRITICAL(&mux_);

    Serial.printf("%s. Server balance now %.3f kWh\n",
                  applied ? "Push confirmed"
                          : "Balance changed on server (top-up), will re-send",
                  serverBalance);
    updateEnergyLogic();
  } else if (reply.error.length() > 0) {
    Serial.printf("Push failed: %s (HTTP %d, usage stays queued)\n",
                  reply.error.c_str(), reply.httpStatus);
  }
}

void PayGoApp::updateEnergyLogic() {
  portENTER_CRITICAL(&mux_);
  const double available = balanceKwh_ - pendingKwh_ - inflightKwh_;
  availableKwh_ = available < 0 ? 0 : available;
  relayDesiredOn_ = availableKwh_ > AppConfig::RELAY_MINIMUM_KWH;
  portEXIT_CRITICAL(&mux_);
}

void PayGoApp::applyOutputState() {
  bool relayDesiredOn;
  portENTER_CRITICAL(&mux_);
  relayDesiredOn = relayDesiredOn_;
  portEXIT_CRITICAL(&mux_);

  if (relay_.isOn() && !relayDesiredOn) {
    saveToStorage();
  }
  relay_.setOn(relayDesiredOn);
}

void PayGoApp::saveToStorage() {
  portENTER_CRITICAL(&mux_);
  EnergySnapshot saved;
  saved.balanceKwh = balanceKwh_;
  saved.pendingKwh = pendingKwh_;
  saved.inflightKwh = inflightKwh_;
  needSave_ = false;
  portEXIT_CRITICAL(&mux_);

  saved.lastPzemKwh = lastPzemKwh_;
  saved.pzemSeeded = pzemSeeded_;
  saved.assignmentState = assignmentState_;
  if (!store_.save(saved)) {
    Serial.println("Could not save meter state; it will be retried.");
    needSave_ = true;
    return;
  }
  lastStorageAt_ = millis();
}

EnergySnapshot PayGoApp::snapshot() {
  portENTER_CRITICAL(&mux_);
  EnergySnapshot current;
  current.balanceKwh = balanceKwh_;
  current.pendingKwh = pendingKwh_;
  current.inflightKwh = inflightKwh_;
  portEXIT_CRITICAL(&mux_);
  current.lastPzemKwh = lastPzemKwh_;
  current.pzemSeeded = pzemSeeded_;
  current.assignmentState = assignmentState_;
  return current;
}

double PayGoApp::availableEnergyKwh() {
  portENTER_CRITICAL(&mux_);
  const double available = availableKwh_;
  portEXIT_CRITICAL(&mux_);
  return available;
}

bool PayGoApp::backendOnline() const {
  const uint32_t lastSuccess = lastSuccessfulHeartbeatAt_;
  return api_.isConnected() && lastSuccess != 0 &&
         millis() - lastSuccess <= AppConfig::BACKEND_ONLINE_TTL_MS;
}

void PayGoApp::logMeterFailure(PzemStatus status, uint8_t bytesReceived) {
  switch (status) {
    case PzemStatus::NO_REPLY:
      Serial.println("PZEM: No reply. Check power and RX/TX.");
      break;
    case PzemStatus::BAD_FRAME:
      Serial.printf("PZEM: Bad frame (%u bytes)\n", bytesReceived);
      break;
    case PzemStatus::BAD_CRC:
      Serial.println("PZEM: CRC mismatch.");
      break;
    case PzemStatus::OK:
      break;
  }
}


PayGoApp payGoApp;
void setup() { payGoApp.setup(); }
void loop() { payGoApp.loop(); }
