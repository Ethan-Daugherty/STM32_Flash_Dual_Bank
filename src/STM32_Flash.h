#include <Particle.h>

enum {
  STM32_RESET_NONINVERTED = 1,
  STM32_BOOT_NONINVERTED = 2
};

// Uncomment to utilize the dual bank architecture for the STM32
#define DUAL_BANK_FLASH

int flashStm32Binary(ApplicationAsset& asset, pin_t boot0Pin, pin_t resetPin, uint32_t options = 0);
