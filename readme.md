

# PAYGO Solar Energy Meter – Firmware

## Overview

A Pay-As-You-Go (PAYGO) solar energy metering system where the device handles local energy deductions while the server maintains the authoritative remaining balance.

The device continuously tracks energy consumption, updates the available balance locally, and synchronizes the remaining balance with the server to ensure accurate billing and prevent top-ups from being overwritten.

## How It Works

### 1. Server as the Source of Truth

- `balanceKwh` represents the last authoritative balance received from the server.
- The server stores and maintains the official remaining energy balance.
- The device uses this balance as its reference for local energy deductions.

### 2. Local Energy Deduction

The device deducts energy consumption locally to ensure that the displayed balance and relay status remain up to date.

The available balance is calculated as:

```text
available = balanceKwh - pending - inflight
````

Where:

- `balanceKwh` — Last balance received from the server.
- `pending` — Energy consumption waiting to be synchronized.
- `inflight` — Energy consumption currently being synchronized.

This ensures the device always reflects the actual available energy, even between server updates.

### 3. Synchronization on Reboot

Whenever the device reboots:

1. It connects to the Wi-Fi network.
2. It synchronizes with the server.
3. It retrieves the latest authoritative energy balance.
4. It updates its local balance.
5. It enables the relay only after a successful synchronization.

This prevents the device from operating with an outdated balance after a restart.

### 4. Periodic Server Synchronization (Heartbeat)

Every 10 seconds, the device sends an HTTP `PUT` request to the server containing the remaining energy balance.

Request payload:

json

```json
{
  "balance": 12.5,
  "expected": 15.0
}
```

Where:

- `balance` — The remaining energy balance calculated by the device.
- `expected` — The last authoritative balance the device received from the server.

The heartbeat also:

- Marks the device as `ONLINE` on the server.
- Synchronizes the remaining energy balance.
- Retrieves updated balances, including newly purchased top-ups.

### 5. Conflict Protection (HTTP 409)

The `expected` field provides optimistic concurrency protection.

Before accepting an update, the server checks whether the current stored balance still matches the device's expected balance.

Example:

1. The device last synchronized a balance of `15 kWh`.
2. The customer purchases an additional `10 kWh`.
3. The server balance becomes `25 kWh`.
4. The device attempts to upload its remaining balance using:
   json
   ```json
   {
     "balance": 12.5,
     "expected": 15.0
   }
   ```
5. The server detects that its current balance (`25 kWh`) no longer matches the expected balance (`15 kWh`).
6. The server rejects the update with `HTTP 409 Conflict`.

This prevents the device from accidentally overwriting newly purchased energy.

The device must then synchronize with the server again to obtain the updated authoritative balance.

## Key Features

| Feature | Description |
| --- | --- |
| Local deductions | Energy usage is deducted directly on the device. |
| Server authority | The server maintains the official remaining balance. |
| Automatic |  |
=======
# PayGo ESP32 energy meter firmware

The firmware targets the hardware wiring already used by the project: an ESP32,
a UART-connected PZEM energy meter, a 16×2 I²C LCD, a load relay, a buzzer, and
an active-low confirmation button.

## Hardware map

| Component | ESP32 connection / setting |
| --- | --- |
| PZEM UART RX / TX | GPIO 16 / GPIO 17, 9600 baud |
| I²C SDA / SCL | GPIO 21 / GPIO 22 |
| LCD address | `0x27`, 16 columns × 2 rows |
| Relay control | GPIO 5, active high |
| Buzzer control | GPIO 23, active high |
| Page button | GPIO 4, active low with internal pull-up |

These settings are preserved from the previous firmware. Confirm the relay
module's electrical polarity and the meter's UART levels against the actual
board before connecting a live load. This refactor does not change the balance
calculation, relay threshold, or offline balance behavior.

## Source layout

- `paygo_firmware.ino` — Arduino entry point.
- `src/HardwareConfig.h` — pin map, timing, and energy thresholds.
- `src/PzemMeter.*` — PZEM request, Modbus CRC, and register conversion.
- `src/BalanceStore.*` — ESP32 Preferences/NVS persistence.
- `src/PayGoApi.*` — Wi-Fi, HTTPS, JSON, heartbeat, and balance update calls.
- `src/FrontPanel.*` — LCD pages and button handling.
- `src/LoadRelay.*` and `src/LowBalanceBuzzer.*` — physical outputs.
- `src/PayGoApp.*` — startup, task coordination, energy accounting, and policy.

## Build with Arduino IDE

1. Install the ESP32 Arduino board package.
2. Install the `ArduinoJson` 7 and `LiquidCrystal_I2C` libraries.
3. Copy `src/DeviceConfig.example.h` to `src/DeviceConfig.h`, then set the
   registered meter ID and API base URL. Do not configure the shared demo
   `DEVICE-001` for a customer meter.
4. Copy `src/Secrets.example.h` to `src/Secrets.h`, then configure Wi-Fi and
   paste the verified CA certificate for the configured API host. Set
   `PAYGO_TLS_CA_CONFIGURED` to `1` only after verifying that certificate chain.
5. Open `paygo_firmware.ino` in Arduino IDE, choose the correct ESP32 board and
   port, then build and flash.

Device-specific config and Wi-Fi credentials are ignored by Git. HTTPS
certificate validation is required; the firmware intentionally has no
`setInsecure()` fallback. If system time or certificate verification fails,
remote sync fails closed and the existing offline stored-balance path remains
available.

## Backend and MQTT integration status

This firmware does **not** connect to MQTT. It has no MQTT client, broker
configuration, device subscription, or MQTT acknowledgement handler. Its
network synchronization currently uses HTTPS heartbeat and balance-update
requests only.

The backend has MQTT credit-delivery and acknowledgement logic, but that is not
connected end to end to this firmware. Do not treat a successful payment, an
HTTPS heartbeat, or a balance update as proof that a physical meter received
and applied a purchase credit. Hardware delivery must remain unconfirmed until
the firmware and backend implement and pass an integrated delivery and
acknowledgement flow.

## Current protocol limitations

The backend's existing heartbeat and balance endpoints do not authenticate a
device-bound cryptographic identity. HTTPS protects the transport, but does not
prove meter ownership to the backend. Do not enable self-service pairing or
treat a configured device ID as ownership verification until the backend and
firmware share a reviewed, device-specific challenge and proof protocol. That
protocol also requires compatible backend authorization and provisioning
changes.

## Runtime behavior

The server remains authoritative for the stored balance. The firmware tracks
local consumption as pending and in-flight energy and uses the existing
optimistic `expected` balance check when syncing. It retains the prior startup
and offline behavior, including the relay cutoff threshold and NVS recovery.

