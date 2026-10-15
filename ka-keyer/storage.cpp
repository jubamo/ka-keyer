#include "settings.hpp"
#include "storage.hpp"

#ifdef ARDUINO_AVR_NANO
#include "EEPROM.h"
#define EEPROM_SIZE 1024
#endif

#ifdef ARDUINO_WAVESHARE_RP2040_ZERO
#include "Wire.h"
#define EEPROM_ADDRESS 0x50
#define EEPROM_SIZE 256
#endif

#define EEPROM_SLOTS (EEPROM_SIZE / SETTINGS_PDATA_SIZE)

static uint8_t localData[SETTINGS_PDATA_SIZE];

static int currentSlot;
static int nextSlot;
static int bytesToWrite;
static unsigned long activationTime;
static unsigned long lastWrite;



// CRC16 of nBytes Polynomial 0x1021, initial value 0xFFFF.
static uint16_t crc16(uint8_t *data, int len) {
  uint16_t crc = 0xFFFF;
  for (int p = 0; p < len; p++) {
    crc = crc ^ ((uint16_t)data[p] << 8);
    for (uint8_t b = 0; b < 8; b++) {
      if (crc & 0x8000) {
        crc = (crc << 1) ^ 0x1021;
      } else {
        crc = crc << 1;
      }
    }
  }
  return crc;
}



static void updateCRC(uint8_t *data){
  uint16_t crc = crc16(data, SETTINGS_PDATA_PAYLOAD_SIZE);
  data[SETTINGS_PDATA_CRC_L] = crc & 0x00FF;
  data[SETTINGS_PDATA_CRC_H] = (crc >> 8) & 0x00FF;
}



#ifdef ARDUINO_AVR_NANO
static void eepromUpdate(int address, uint8_t data){
  EEPROM.update(address, data);
}
#endif



#ifdef ARDUINO_WAVESHARE_RP2040_ZERO

void storageBegin(uint8_t scl, uint8_t sda){
  Wire.setSCL(scl);
  Wire.setSDA(sda);
  Wire.begin();
  Wire.setClock(400000);
}



static void writeI2CByte(int data_addr, uint8_t data){
  Wire.beginTransmission(EEPROM_ADDRESS);
  Wire.write(data_addr);
  Wire.write(data);
  Wire.endTransmission();
}



static uint8_t readI2CByte(int data_addr){
  uint8_t data = 0;
  Wire.beginTransmission(EEPROM_ADDRESS);
  Wire.write(data_addr);
  Wire.endTransmission();
  Wire.requestFrom(EEPROM_ADDRESS, 1); //retrieve 1 returned byte
  if(Wire.available()){
    data = Wire.read();
  }
  return data;
}



static void eepromUpdate(int address, uint8_t data){
  uint8_t val = readI2CByte(address);
  if (val != data){
    writeI2CByte(address, data);
  }
}



static void eepromGet(int address, uint8_t *data){
  for (int n = 0; n < SETTINGS_PDATA_SIZE; n++){
    data[n] = readI2CByte(address + n);
  }
}

#endif



void storageLoad(uint8_t *data){
  uint16_t crc, slotcrc;
  // Look for a slot with valid CRC
  for (currentSlot = 0; currentSlot < EEPROM_SLOTS; currentSlot++) {

#ifdef ARDUINO_AVR_NANO    
    EEPROM.get(currentSlot * SETTINGS_PDATA_SIZE, localData);
#endif
#ifdef ARDUINO_WAVESHARE_RP2040_ZERO
    eepromGet(currentSlot * SETTINGS_PDATA_SIZE, localData);
#endif

    crc = crc16(localData, SETTINGS_PDATA_PAYLOAD_SIZE);
    slotcrc = ((uint16_t)localData[SETTINGS_PDATA_CRC_H] << 8) | (uint16_t)localData[SETTINGS_PDATA_CRC_L];

    if (crc == slotcrc) {
      // Good CRC
      memcpy(data, localData, SETTINGS_PDATA_SIZE);
      break;
    }
  }

  if (currentSlot == EEPROM_SLOTS) {
    // For loop didn't find a valid slot. Point to
    // EEPROM_SLOTS - 1 so the first write will be at 0
    currentSlot = EEPROM_SLOTS - 1;
  }
}



void storageSave(uint8_t *data){
  // Make a local copy from the data to be saved
  memcpy(localData, data, SETTINGS_PDATA_SIZE);
  updateCRC(localData);
  activationTime = millis();
  lastWrite = activationTime;
  nextSlot = (currentSlot + 1) % EEPROM_SLOTS;
  bytesToWrite = SETTINGS_PDATA_SIZE + 2;
}



void storageProcess(){
  unsigned long now;
  int offset;

  if (bytesToWrite){
    now = millis();
    if ((now - activationTime) > 10000) { // Ten seconds
      if ((now - lastWrite) > 10) { // 10ms between writes
        switch (bytesToWrite){
          case 1:
            // Invalidates current slot CRC
            eepromUpdate((currentSlot * SETTINGS_PDATA_SIZE) + (SETTINGS_PDATA_CRC_H), 0xFF);
            break;
          case 2:
            // Invalidates current slot CRC
            eepromUpdate((currentSlot * SETTINGS_PDATA_SIZE) + (SETTINGS_PDATA_CRC_L), 0xFF);
            break;
          default:
            offset = bytesToWrite - 3;
            eepromUpdate((nextSlot * SETTINGS_PDATA_SIZE) + offset, localData[offset]);
        }
        bytesToWrite--;
        lastWrite = now;
      }
    }
  }
}
