# PAYGO ESP32 Meter Firmware

Firmware for the PAYGO solar energy meter built around an ESP32, PZEM-004T, 12×2 I²C LCD, load relay, buzzer and confirmation button.

## Hardware map

| Component | ESP32 connection / setting |
| --- | --- |
| PZEM UART RX / TX | GPIO 16 / GPIO 17, 9600 baud |
| I²C SDA / SCL | GPIO 21 / GPIO 22 |
| LCD | I²C address `0x27`, 12 columns × 2 rows |
| Relay control | GPIO 5, active high |
| Buzzer control | GPIO 23, active high |
| Page button | GPIO 4, active low with internal pull-up |

The existing pin assignments and balance/relay policy are preserved. Confirm the relay module polarity and PZEM UART electrical levels against the actual board before connecting a live load.

## LCD dashboard

The LCD rotates through three pages every three seconds. The GPIO 4 button can also advance to the next page.

1. **Live electrical readings**
   - `V` — voltage in volts
   - `F` — frequency in hertz
   - `I` — current in amperes
   - `P` — active power in watts

2. **Energy and credit**
   - `E` — cumulative energy reported by the PZEM in kWh
   - `B` — locally available prepaid balance in kWh, after pending and in-flight consumption

3. **Device and connection status**
   - Line 1 shows the PAYGO label and the final four numeric digits of `PAYGO_DEVICE_ID`, e.g. `PAYGO #0001`.
   - Line 2 uses `ONLINE|ACTV`, `OFFLINE|ACTV`, `ONLINE|UNAS` or `OFFLINE|UNAS`.
   - `ONLINE` means a successful HTTPS heartbeat was received within the 15-second freshness window while Wi-Fi is connected.
   - `ACTV` means the backend reports the meter assignment as `ACTIVATED`; `UNAS` means it is not activated.
   - The activation state is cached in ESP32 Preferences so it remains meaningful during a temporary network outage.

The LCD is configured for exactly 12 columns. The status labels are intentionally shortened to fit. The firmware updates only lines whose displayed text has changed and does not clear the LCD on every refresh. Failed PZEM reads are shown as dashes rather than presenting old electrical readings as current.

## Balance and synchronization behavior

- The backend remains the authoritative source for the prepaid balance.
- The ESP32 tracks pending and in-flight consumption locally.
- Available balance is calculated as `balanceKwh - pendingKwh - inflightKwh`.
- The firmware periodically sends HTTPS heartbeat and balance updates.
- Balance updates retain the existing optimistic `expected` balance check to avoid overwriting a concurrent top-up.
- Balance and activation state are stored in ESP32 Preferences for recovery after reboot.
- HTTPS certificate validation is required. There is no insecure TLS fallback.

The ONLINE indicator reflects the HTTPS backend heartbeat, not MQTT connectivity.

## Backend integration status

This firmware currently uses HTTPS heartbeat and balance endpoints. It does **not** implement an MQTT client, MQTT credit subscription, or MQTT acknowledgement handler. The backend's MQTT credit-delivery flow is therefore not yet connected end to end to this firmware. Do not treat a successful payment or backend heartbeat as proof that a physical meter received and applied a purchase credit.

The heartbeat response includes the backend's `assignmentStatus`. Deploy the matching PAYGO backend update before relying on the LCD's `ACTV` / `UNAS` indicator.

The current device heartbeat endpoints do not yet provide a reviewed device-bound cryptographic identity protocol. Do not use a device ID alone as proof of device ownership or enable self-service pairing based on this firmware.

## Build with Arduino IDE

1. Install the ESP32 Arduino board package.
2. Install the ArduinoJson 7 and LiquidCrystal_I2C libraries.
3. Copy `src/DeviceConfig.example.h` to `src/DeviceConfig.h` and set the registered physical meter ID and API base URL. Use a unique ID with a numeric suffix for the LCD label.
4. Copy `src/Secrets.example.h` to `src/Secrets.h` and configure Wi-Fi credentials and the verified CA certificate for the API host. Set `PAYGO_TLS_CA_CONFIGURED` to `1` only after verifying the certificate chain.
5. Open `paygo_firmware.ino`, select the correct ESP32 board and port, compile, then flash the board.

Device-specific configuration and Wi-Fi credentials are ignored by Git and must be configured locally before building.
