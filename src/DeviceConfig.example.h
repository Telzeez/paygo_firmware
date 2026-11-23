#pragma once

// Copy this file to DeviceConfig.h and set the identity of this physical meter.
// Do not use the shared demo meter ID for a production device.
#define PAYGO_DEVICE_ID "YOUR_REGISTERED_DEVICE_ID"
#define PAYGO_API_BASE_URL "https://paygo-backend-6w4q.onrender.com"

// Generate this credential from the PAYGO administrator dashboard.
// Keep it unique per physical meter and never commit your local DeviceConfig.h.
#define PAYGO_DEVICE_API_KEY "PASTE_ONE_TIME_DEVICE_API_KEY_HERE"
