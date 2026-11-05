#include "PayGoApi.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <cmath>
#include <cstring>
#include <ctime>

#include "HardwareConfig.h"

#if !__has_include("DeviceConfig.h")
#error "Copy DeviceConfig.example.h to DeviceConfig.h and set the registered device ID."
#endif
#include "DeviceConfig.h"

#if !__has_include("Secrets.h")
#error "Copy Secrets.example.h to Secrets.h and configure Wi-Fi and the HTTPS CA certificate."
#endif
#include "Secrets.h"

#if !defined(PAYGO_TLS_CA_CONFIGURED) || PAYGO_TLS_CA_CONFIGURED != 1
#error "Configure and verify PAYGO_TLS_ROOT_CA_PEM before building; TLS verification must remain enabled."
#endif

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
  return sendRequest("POST", deviceEndpoint("/heartbeat"), "{}");
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

  if (strcmp(method, "POST") == 0) {
    result.httpStatus = http.POST(body);
  } else {
    result.httpStatus = http.PUT(body);
  }

  if (result.httpStatus > 0) {
    const String responseBody = http.getString();
    result.hasBalance = parseBalance(responseBody, result.balanceKwh);
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
