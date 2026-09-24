#ifndef FRAM_CONTROLLER_H
#define FRAM_CONTROLLER_H

#include <Arduino.h>
#include "fram_constants.h"

// ============================================================
// FRAM I2C (FM24W256 / MB85RC256V, 0x50) — warstwa blokowa.
// Własny sterownik na Wire zamiast Adafruit_FRAM_I2C: biblioteka czyta i pisze
// bajt po bajcie (5 B transakcji na 1 B danych). Tu porcje po FRAM_CHUNK bajtów
// (bufor Wire na ESP32 = 128 B, 2 B idą na adres).
// Wire musi być zainicjalizowany wcześniej (initI2CBus()).
// ============================================================

void initI2CBus();                  // Wire.begin + timeout; wspólna magistrala z DS3231
bool i2cBusRecover();               // 9 impulsów SCL + STOP, ponowny Wire.begin

bool initFRAM();
bool isFramInitialized();

bool framRead(uint16_t addr, void* buf, size_t len);
bool framWrite(uint16_t addr, const void* buf, size_t len);   // z odczytem kontrolnym

uint32_t crc32Calc(const void* data, size_t len, uint32_t crc = 0);

// ===============================
// CREDENTIALS (provisioning, credentials_manager — API bez zmian z termostatu)
// ===============================
struct FRAMCredentials;

bool readCredentialsFromFRAM(FRAMCredentials& creds);
bool writeCredentialsToFRAM(const FRAMCredentials& creds);
bool verifyCredentialsInFRAM();

#endif
