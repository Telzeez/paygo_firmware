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
   - Line 2 combines network and assignment state, for example `ONLINE|ACTV`, `OFFLINE|ACTV`, `ONLINE|ASGN`, `ONLINE|UNAS` or `ONLINE|SUSP`.
   - `ONLINE` means a successful HTTPS heartbeat was received within the 15-second freshness window while Wi-Fi is connected.
   - `ACTV` means the backend reports `ACTIVATED`; `ASGN` means assigned but awaiting owner activation; `UNAS` means unassigned; `SUSP` means suspended.
   - The assignment state is cached in ESP32 Preferences so it remains meaningful during a temporary network outage.

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

## Arduino IDE project

This repository is already arranged as a multi-file Arduino IDE sketch. Do not open individual files under `src/`; open the root sketch named `paygo_firmware.ino`.

Project layout:

```text
paygo_firmware/
├── paygo_firmware.ino
└── src/
    ├── BalanceStore.cpp
    ├── BalanceStore.h
    ├── DeviceConfig.example.h
    ├── DeviceState.h
    ├── FrontPanel.cpp
    ├── FrontPanel.h
    ├── HardwareConfig.h
    ├── LoadRelay.cpp
    ├── LoadRelay.h
    ├── LowBalanceBuzzer.cpp
    ├── LowBalanceBuzzer.h
    ├── PayGoApi.cpp
    ├── PayGoApi.h
    ├── PayGoApp.cpp
    ├── PayGoApp.h
    ├── PzemMeter.cpp
    ├── PzemMeter.h
    └── Secrets.example.h
```

### 1. Install the ESP32 board package

In Arduino IDE, open **File → Preferences** and add Espressif's ESP32 Boards Manager URL if it is not already listed:

`https://espressif.github.io/arduino-esp32/package_esp32_index.json`

Then open **Tools → Board → Boards Manager**, search for **esp32**, and install **esp32 by Espressif Systems**.

### 2. Install the libraries

Open **Sketch → Include Library → Manage Libraries** and install:

- **ArduinoJson** by Benoit Blanchon, version 7.x.
- **LiquidCrystal I2C** by Frank de Brabander.

The PZEM-004T Modbus reader is implemented in this project; no separate PZEM library is required.

### 3. Add this meter's configuration

Inside the extracted project folder, in `src/`:

1. Make a copy of `DeviceConfig.example.h` named `DeviceConfig.h`.
2. Set `PAYGO_DEVICE_ID` to the registered ID printed on this physical meter. Use a unique ID with a numeric suffix, such as `PAYGO-ESP32-0001`.
3. Keep `PAYGO_API_BASE_URL` set to the production backend URL unless the deployment changes.
4. Make a copy of `Secrets.example.h` named `Secrets.h`.
5. Enter the Wi-Fi SSID and password, and add the verified root CA certificate for the API host. Set `PAYGO_TLS_CA_CONFIGURED` to `1` only after adding and verifying the correct certificate.

Do not upload `Secrets.h` or `DeviceConfig.h` to GitHub. Both are excluded by `.gitignore`.

### 4. Select the board and upload

1. Open `paygo_firmware.ino` in Arduino IDE.
2. Select **Tools → Board → esp32 → ESP32 Dev Module** (or the exact ESP32 board you have).
3. Select the ESP32's serial port.
4. Select a suitable upload speed, such as 115200, if required by your board.
5. Click **Verify** first. Resolve any compile errors before connecting a live load.
6. Click **Upload** to flash the ESP32.
7. Open **Tools → Serial Monitor** at 115200 baud to inspect startup, PZEM and backend messages.

### LCD and wiring assumptions

The sketch is configured for a 12-column × 2-row I²C LCD at address `0x27`, with SDA on GPIO 21 and SCL on GPIO 22. It assumes the existing pin map in `src/HardwareConfig.h`. If your LCD backpack uses another I²C address, update `LCD_ADDRESS` before uploading.

**Safety:** Test the firmware with the low-voltage/control side first. Verify relay contact ratings, isolation, fuse protection, PZEM wiring and relay active level against the assembled hardware before connecting mains or a live load. Never connect mains voltage to ESP32 GPIO pins.

Device-specific configuration and Wi-Fi credentials are ignored by Git and must be configured locally before building.
