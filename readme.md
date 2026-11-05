
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
