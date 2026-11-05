#pragma once

// Copy this file to Secrets.h. Keep Secrets.h out of version control.
#define PAYGO_WIFI_SSID "YOUR_WIFI_SSID"
#define PAYGO_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// Add the verified CA certificate for PAYGO_API_BASE_URL as a PEM string.
// Set PAYGO_TLS_CA_CONFIGURED to 1 only after validating the certificate chain.
#define PAYGO_TLS_CA_CONFIGURED 0
static const char PAYGO_TLS_ROOT_CA_PEM[] PROGMEM = R"PAYGO_CA(
-----BEGIN CERTIFICATE-----
REPLACE_WITH_VERIFIED_CA_CERTIFICATE
-----END CERTIFICATE-----
)PAYGO_CA";
