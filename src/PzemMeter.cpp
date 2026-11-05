#include "PzemMeter.h"

#include "HardwareConfig.h"

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
