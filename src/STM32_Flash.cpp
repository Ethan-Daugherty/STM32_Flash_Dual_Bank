/*
 * Library to flash an asset to an STM32 microcontroller using UART, BOOT0 and RESET pins
 */

#define LOG_CHECKED_ERRORS 1
#include "STM32_Flash.h"

// STM32 communication parameters
const auto BAUD_RATE = 115200;

const auto COMMAND_TIMEOUT = 1000;
const auto ERASE_FLASH_TIMEOUT = 1000;
const auto WRITE_BLOCK_TIMEOUT = 1000;

// STM32 bootloader commands
const uint8_t GET_INFO = 0x00;
const uint8_t WRITE_MEMORY = 0x31;
const uint32_t READ_MEMORY = 0x11;
const uint8_t ERASE_FLASH = 0x43;
const uint8_t ERASE_FLASH_EXTENDED = 0x44;
const uint8_t ENTER_BOOTLOADER = 0x7F;
const uint8_t ACK = 0x79;

#ifdef DUAL_BANK_FLASH
// ##### Values will change for different STM32 microcontrollers #####
// STM32 "logical" memory address for inactive bank
const uint32_t FLASH_START = 0x08040000;

// Address where FLASH_OPTR register is and is the beginning of the option byte area of an STM32
// This register contains the SWAP_BANK bit to determine if the banks have been swapped or to swap the banks
const uint32_t FLASH_OPTR_ADDR = 0x40022040;
// Position where SWAP_BANK bit is 
const uint32_t swapBitPosition = 20;

// Address where FLASH nonsecure status register is
const uint32_t FLASH_NSSR = 0x40022020;
// BSY bit position in FLASH_NSSR
const uint32_t BSY_BIT_POSITION = 16;

// Amount of memory pages per bank
const uint8_t PAGE_AMOUNT = 32;

// Count of the number of option byte registers 
const int numOfOptionByteReg = 12;

// Variable used to determine success of the flash
bool flashSuccess = false;

// Functions for Dual Bank Flash
int getBankPages(uint8_t* data, size_t length);
bool ifBankSwapped();
int readFromMemory(uint32_t address, uint8_t* data, uint8_t bytesToRead);
int swapBanks();
int waitForBSYClear(int timeout = COMMAND_TIMEOUT);
uint8_t xorChecksum(const uint8_t* data, size_t length);
#else
// STM32 memory map
const uint32_t FLASH_START = 0x08000000;
#endif

bool useExtendedErase = false;

void setupStm32(pin_t boot0Pin, pin_t resetPin);
int performFlashSteps(ApplicationAsset& asset, pin_t boot0Pin, pin_t resetPin, uint32_t options);
int enterBootloader(pin_t boot0Pin, pin_t resetPin, uint32_t options);
void resetStm32(pin_t resetPin, uint32_t options);
void exitBootloader(pin_t boot0Pin, pin_t resetPin, uint32_t options);
int getInfo();
int eraseFlash();
int flashBinary(ApplicationAsset& asset);
int writeBlock(uint32_t address, uint8_t* data, uint16_t size);
int sendCommand(uint8_t command, uint16_t timeout = COMMAND_TIMEOUT);
int waitForAck(uint8_t command, int timeout = COMMAND_TIMEOUT);

int flashStm32Binary(ApplicationAsset& asset, pin_t boot0Pin, pin_t resetPin, uint32_t options) {
  setupStm32(boot0Pin, resetPin);
  int ret = performFlashSteps(asset, boot0Pin, resetPin, options);
  exitBootloader(boot0Pin, resetPin, options);
  return ret;
}

void setupStm32(pin_t boot0Pin, pin_t resetPin) {
  // Set the GPIO pins as output
  pinMode(boot0Pin, OUTPUT);
  pinMode(resetPin, OUTPUT);

  // Initialize the UART
  // The STM32 bootloader expects even parity
  Serial1.begin(BAUD_RATE, SERIAL_PARITY_EVEN);
}

#ifdef DUAL_BANK_FLASH
int performFlashSteps(ApplicationAsset& asset, pin_t boot0Pin, pin_t resetPin, uint32_t options) {
  CHECK(enterBootloader(boot0Pin, resetPin, options));
  CHECK(getInfo());
  CHECK(eraseFlash());
  CHECK(flashBinary(asset));
  CHECK(swapBanks());
  return SYSTEM_ERROR_NONE;
}
#else
int performFlashSteps(ApplicationAsset& asset, pin_t boot0Pin, pin_t resetPin, uint32_t options) {
  CHECK(enterBootloader(boot0Pin, resetPin, options));
  CHECK(getInfo());
  CHECK(eraseFlash());
  CHECK(flashBinary(asset));
  return SYSTEM_ERROR_NONE;
}
#endif

int enterBootloader(pin_t boot0Pin, pin_t resetPin, uint32_t options) {
  LOG(INFO, "Resetting STM32 into bootloader mode");

  // Set BOOT0 pin to enter the bootloader mode
  digitalWrite(boot0Pin, (options & STM32_BOOT_NONINVERTED) ? HIGH : LOW);
  delay(1);

  // Reset the STM32
  resetStm32(resetPin, options);

  delay(10);

  // Send the bootloader start command
  Serial1.write(ENTER_BOOTLOADER);

  // Wait to get acknowledgement
  for (int i = 0; i < 100; i++) {
    waitFor(Serial1.available, 100);
    if (Serial1.available()) {
      auto resp = Serial1.read();
      if (resp == ACK) {
        LOG(INFO, "STM32 in bootloader mode");
        return SYSTEM_ERROR_NONE;
      } else {
        LOG(INFO, "Ignoring unexpected response from STM32: 0x%02x", resp);
      }
    }
  }
  return SYSTEM_ERROR_TIMEOUT;
}

void resetStm32(pin_t resetPin, uint32_t options) {
  digitalWrite(resetPin, (options & STM32_RESET_NONINVERTED) ? LOW : HIGH);
  delay(1);
  digitalWrite(resetPin, (options & STM32_RESET_NONINVERTED) ? HIGH : LOW);
  delay(1);
}

void exitBootloader(pin_t boot0Pin, pin_t resetPin, uint32_t options) {
  // Set BOOT0 pin to run the program
  digitalWrite(boot0Pin, (options & STM32_BOOT_NONINVERTED) ? LOW : HIGH);
  delay(1);

  resetStm32(resetPin, options);
}

int getInfo() {
  CHECK(sendCommand(GET_INFO));

  // info response shows which erase command to use
  while (true) {
    waitFor(Serial1.available, 100);
    if (!Serial1.available()) {
      LOG(ERROR, "Timeout waiting for get info response");
      return SYSTEM_ERROR_TIMEOUT;
    }
    auto resp = Serial1.read();
    switch (resp) {
      case ERASE_FLASH:
        useExtendedErase = false;
        break;
      case ERASE_FLASH_EXTENDED:
        useExtendedErase = true;
        break;
      case ACK:
        return SYSTEM_ERROR_NONE;
    }
  }
}

#ifdef DUAL_BANK_FLASH
int eraseFlash() {
  LOG(INFO, "Erasing STM32 flash");
  if (useExtendedErase) {
    LOG(INFO, "ERASE_FLASH_EXTENDED command used");

    // Avoid timout errors by collecting bankSwap before command sequence
    bool bankSwap = ifBankSwapped();

    CHECK(sendCommand(ERASE_FLASH_EXTENDED));
    // If banks are swapped the inactive bank is Bank 1
    if(bankSwap){
      // Bank 1 erase
      uint8_t buf[3] = { 0xFF, 0xFE, 0x01 };
      Serial1.write(buf, sizeof(buf));
    }
    else{
      // Bank 2 erase
      uint8_t buf[3] = { 0xFF, 0xFD, 0x02 };
      Serial1.write(buf, sizeof(buf));
    }

    CHECK(waitForAck(ERASE_FLASH_EXTENDED));
  } else {
    LOG(INFO, "ERASE_FLASH command used");

    CHECK(sendCommand(ERASE_FLASH));

    // First byte after the command is the amount of pages to erase - 1
    Serial1.write((PAGE_AMOUNT-1));

    // Create array to hold message of the page numbers to erase
    uint8_t buf[PAGE_AMOUNT];
  
    // Erase command requires the numbers of which pages to erase
    CHECK(getBankPages(buf, sizeof(buf)));
    
    // Send the page numbers to erase
    Serial1.write(buf, sizeof(buf));

    // Checksum for the pages to be erased includes how many pages to erase - 1 and the bytes that tell which pages to erase
    uint8_t eraseCheckSum = (PAGE_AMOUNT - 1) ^ xorChecksum(buf, sizeof(buf));
    Serial1.write(eraseCheckSum);

    // Erase Inactive Bank
    CHECK(waitForAck(ERASE_FLASH, ERASE_FLASH_TIMEOUT));
  }

  LOG(INFO, "Erased STM32 flash");
  return SYSTEM_ERROR_NONE;
}
#else
int eraseFlash() {
  LOG(INFO, "Erasing STM32 flash");
  if (useExtendedErase) {
    CHECK(sendCommand(ERASE_FLASH_EXTENDED));

    // Global erase
    uint8_t buf[3] = { 0xFF, 0xFF, 0x00 };
    Serial1.write(buf, sizeof(buf));

    CHECK(waitForAck(ERASE_FLASH_EXTENDED));
  } else {
    CHECK(sendCommand(ERASE_FLASH));

    // Erase all blocks
    CHECK(sendCommand(0xFF, ERASE_FLASH_TIMEOUT));
  }

  LOG(INFO, "Erased STM32 flash");
  return SYSTEM_ERROR_NONE;
}
#endif

int flashBinary(ApplicationAsset& asset) {
  LOG(INFO, "Flashing STM32 binary");
  auto address = FLASH_START;

  while (asset.available()) {
    uint8_t buf[256];
    int read = asset.read((char*) buf, sizeof(buf));
    if (read < 0) {
      LOG(ERROR, "Error %d reading binary from asset", read);
    }

    CHECK(writeBlock(address, buf, read));
    address += read;
  }
  LOG(INFO, "Flashed STM32");

  #ifdef DUAL_BANK_FLASH
  flashSuccess = true;
  #endif

  return SYSTEM_ERROR_NONE;
}

int writeBlock(uint32_t address, uint8_t* data, uint16_t size) {
  // Uncomment to debug write block
  // LOG(DEBUG, "Writing %d bytes to 0x%08x", size, address);

  // Send the write command
  CHECK(sendCommand(WRITE_MEMORY));

  // write the address and checksum
  uint8_t buf[5];
  buf[0] = address >> 24;
  buf[1] = (address >> 16) & 0xFF;
  buf[2] = (address >> 8) & 0xFF;
  buf[3] = address & 0xFF;
  buf[4] = buf[0] ^ buf[1] ^ buf[2] ^ buf[3];
  Serial1.write(buf, sizeof(buf));

  CHECK(waitForAck(WRITE_MEMORY));

  // send the number of bytes, the data and the checksum
  uint16_t alignedSize = (size + 3) & ~0x03;
  uint8_t sizeTx = (alignedSize - 1) & 0xFF;
  Serial1.write(&sizeTx, sizeof(sizeTx));
  Serial1.write(data, size);
  for (auto i = size; i < alignedSize; i++) {
    Serial1.write(0xFF);
  }

  uint8_t checksum = sizeTx;
  for (auto i = 0; i < size; i++) {
    checksum ^= data[i];
  }
  for (auto i = size; i < alignedSize; i++) {
    checksum ^= 0xFF;
  }
  Serial1.write(checksum);

  CHECK(waitForAck(WRITE_MEMORY, WRITE_BLOCK_TIMEOUT));

  return SYSTEM_ERROR_NONE;
}

int sendCommand(uint8_t command, uint16_t timeout) {
  // each command must be written twice: once as is and once with the complement
  uint8_t buf[] = { command, (uint8_t) (command ^ 0xFF) };
  Serial1.write(buf, sizeof(buf));

  // wait for response
  return waitForAck(command, timeout);
}

int waitForAck(uint8_t command, int timeout) {
  waitFor(Serial1.available, timeout);
  if (!Serial1.available()) {
    LOG(ERROR, "Timeout waiting for STM32 to acknowledge command 0x%02x", command);
    return SYSTEM_ERROR_TIMEOUT;
  }

  auto resp = Serial1.read();
  if (resp != ACK) {
    LOG(ERROR, "Unexpected response from STM32 after command 0x%02x: 0x%02x", command, resp);
    return SYSTEM_ERROR_INVALID_STATE;
  }

  return SYSTEM_ERROR_NONE;
}

#ifdef DUAL_BANK_FLASH
// Function to get memory bank page numbers for erase command
// STM32 memory pages address physiscal memory not the logical memory
int getBankPages(uint8_t* data, size_t length){
  (void)length; // Unused parameter - length not needed for current implementation
  int count = 0;
  int firstPage = 0;
  int lastPage = 0;

  // If banks are swapped means the inactive bank is Bank 1
  if(ifBankSwapped()){
    // Starting page number of Bank 1
    firstPage = 0;
    // Last page number of Bank 1
    lastPage = PAGE_AMOUNT;
  }
  else{
    // Starting page number of Bank 2
    firstPage = PAGE_AMOUNT;
    // Last page number in Bank 2
    lastPage = firstPage + PAGE_AMOUNT;
  }
  
  // Fill buffer with numbers in HEX/byte form
  for (int i = firstPage; i < lastPage && count < PAGE_AMOUNT; i++) {
      data[count++] = static_cast<uint8_t>(i & 0xFF);
  }
  
  return SYSTEM_ERROR_NONE;
}

// Function to determine if banks are swapped
bool ifBankSwapped(){
  bool bankSwapped = false;
  uint8_t bytesToRead[4];
  uint32_t FLASH_OPTR = 0;
  CHECK(readFromMemory(FLASH_OPTR_ADDR, bytesToRead, 4));
  FLASH_OPTR = ((uint32_t)bytesToRead[3] << 24) |
               ((uint32_t)bytesToRead[2] << 16) |
               ((uint32_t)bytesToRead[1] << 8)  |
               ((uint32_t)bytesToRead[0]);
               
  LOG(INFO, "FLASH_OPTR: 0x%08X", FLASH_OPTR);
  bankSwapped = (FLASH_OPTR >> swapBitPosition) & 1;
  LOG(INFO, bankSwapped ? "Banks are swapped" : "Banks not swapped");

  return bankSwapped;
}

// Function to read data from a memory location in the STM32
// Read memory command on pg.15 of AN3155 USART protocol used in the STM32 bootloader
// https://www.st.com/resource/en/application_note/an3155-usart-protocol-used-in-the-stm32-bootloader-stmicroelectronics.pdf
int readFromMemory(uint32_t address, uint8_t* data, uint8_t bytesToRead){
  CHECK(sendCommand(READ_MEMORY));
  LOG(INFO, "Read Memory Command Sent");

  // Separate address out in different bytes
  uint8_t buf[4] = {static_cast<uint8_t>((address >> 24) & 0xFF),
                    static_cast<uint8_t>((address >> 16) & 0xFF),
                    static_cast<uint8_t>((address >> 8) & 0xFF),
                    static_cast<uint8_t>((address >> 0) & 0xFF)
                   };

  // Retrieve the checksum for the address
  uint8_t addressCheckSum = xorChecksum(buf, sizeof(buf));
  // Send address and checksum 
  Serial1.write(buf, sizeof(buf));
  Serial1.write(addressCheckSum);
  LOG(INFO, "Memory Address Checksum Sent");
  CHECK(waitForAck(READ_MEMORY));

  // Write how many bytes to read - 1
  uint8_t numOfBytes = bytesToRead - 1;
  Serial1.write(numOfBytes);
  LOG(INFO, "Number of Bytes to read Sent");
  
  // Send Checksum(complement to numOfBytes) for how many bytes to read 
  uint8_t numCheckSum = ~numOfBytes;
  Serial1.write(numCheckSum);
  LOG(INFO, "Number of Bytes Checksum Sent");
  CHECK(waitForAck(READ_MEMORY));

  // Receive the data read from the memory
  for(int i = 0; i < bytesToRead; i++){
    data[i] = Serial1.read();
    LOG(INFO, "Data From Memory: 0x%02x", data[i]);
  }

  return SYSTEM_ERROR_NONE;
}

// Determine if banks should be swapped
int swapBanks(){
  // Retrieve the current value of the FLASH_OPTR register
  if(flashSuccess){
    uint32_t FLASH_OPTR = 0;
    uint8_t optionByteArea[numOfOptionByteReg * 4];
    // Read whole option byte area because a write will erase all of them 
    CHECK(readFromMemory(FLASH_OPTR_ADDR, optionByteArea, sizeof(optionByteArea)));
    // Take the returned little endian format of the read and create uint32_t for easy manipulation
    FLASH_OPTR = ((uint32_t)optionByteArea[3] << 24) |
                ((uint32_t)optionByteArea[2] << 16) |
                ((uint32_t)optionByteArea[1] << 8)  |
                ((uint32_t)optionByteArea[0]);

    LOG(INFO, "FLASH_OPTR: 0x%08X", FLASH_OPTR);

    // Swap SWAP_BANK bit using XOR logic
    FLASH_OPTR ^= (1 << swapBitPosition);

    // Split new value of FLASH_OPTR into bytes to be sent
    // STM32 architecture is little endian so data fields must be sent in that format
    optionByteArea[0] = static_cast<uint8_t>((FLASH_OPTR >> 0) & 0xFF);
    optionByteArea[1] = static_cast<uint8_t>((FLASH_OPTR >> 8) & 0xFF);
    optionByteArea[2] = static_cast<uint8_t>((FLASH_OPTR >> 16) & 0xFF);
    optionByteArea[3] = static_cast<uint8_t>((FLASH_OPTR >> 24) & 0xFF);
    
    LOG(INFO, "NEW_FLASH_OPTR: 0x%08X", FLASH_OPTR);
    // Uncomment to debug option byte area 
    // for(int i = 0; i < sizeof(optionByteArea); i++){
    //   LOG(INFO, "optionByteArea(%u): 0x%02X", i, optionByteArea[i]);
    // }

    // Write new value of swap bit to swap banks
    CHECK(writeBlock(FLASH_OPTR_ADDR, optionByteArea, sizeof(optionByteArea)));
    CHECK(waitForBSYClear(3000));

    if(ifBankSwapped()){
      LOG(INFO, "STM32 running from Bank 2.");
    }
    else{
      LOG(INFO, "STM32 running from Bank 1.");
    }

    // Reset flashSuccess variable
    flashSuccess = false;

    return SYSTEM_ERROR_NONE;
  }
  else{
    // if failed to flash no swap should occur
    LOG(ERROR, "STM32 Banks were not swapped because flash was not successful.");
    return SYSTEM_ERROR_ABORTED;
  }
}

/**
 * Wait for the BSY (Busy) bit in FLASH_NSSR to be cleared
 * This indicates that a flash memory operation is complete
 * 
 * @param timeout Maximum time to wait in milliseconds
 * @return SYSTEM_ERROR_NONE if BSY cleared, SYSTEM_ERROR_TIMEOUT if timeout occurred
 */
int waitForBSYClear(int timeout) {
  LOG(INFO, "Waiting for BSY bit to clear");
  uint32_t startTime = millis();
  
  while ((millis() - startTime) < (uint32_t)timeout) {
    uint8_t bytesToRead[4];
    int ret = readFromMemory(FLASH_NSSR, bytesToRead, 4);
    
    // If read fails, continue polling (don't fail immediately)
    if (ret != SYSTEM_ERROR_NONE) {
      delay(10);
      continue;
    }
    
    // Reconstruct the 32-bit FLASH_NSSR value
    uint32_t FLASH_NSSR_VAL = ((uint32_t)bytesToRead[3] << 24) |
                               ((uint32_t)bytesToRead[2] << 16) |
                               ((uint32_t)bytesToRead[1] << 8)  |
                               ((uint32_t)bytesToRead[0]);
    
    // Check if BSY bit is cleared (0 = not busy)
    if (!(FLASH_NSSR_VAL & (1 << BSY_BIT_POSITION))) {
      LOG(INFO, "BSY bit cleared - flash operation complete");
      return SYSTEM_ERROR_NONE;
    }
    
    // Small delay between polls to avoid overwhelming the STM32
    delay(50);
  }
  
  LOG(ERROR, "Timeout waiting for BSY bit to clear in FLASH_NSSR");
  return SYSTEM_ERROR_TIMEOUT;
}

// Function to get check sum for data array 
// STM32 in bootloader uses a xor checksum
uint8_t xorChecksum(const uint8_t* data, size_t length) {
    uint8_t checksum = 0;
    for (size_t i = 0; i < length; i++) {
        checksum ^= data[i];
    }
    return checksum;
}
#endif