# **Firmware in-the-field Updates for STM32**

### **Operations of STM32U5**

* STM32U545 has dual-bank boot mode
* Read-while-write functionality
* Use FLASH\_OPTR register(pg 342, [STM32U5 Reference Manual](https://www.st.com/content/ccc/resource/technical/document/reference_manual/group0/f3/60/ca/d2/98/c8/47/88/DM00477635/files/DM00477635.pdf/jcr:content/translations/en.DM00477635.pdf)):
  bit 21=DUAL\_BANK(0 single bank config, 1 dual bank config)
  bit 20=SWAP\_BANK(0 Bank 1 and Bank 2 addresses not swapped, 1 Bank1 and Bank2 addresses are swapped)

* Active bank memory address= 0x0800 0000
* Inactive bank memory address= 0x0804 0000
* SWAP\_BANK(pg 313, [STM32U5 Reference Manual](https://www.st.com/content/ccc/resource/technical/document/reference_manual/group0/f3/60/ca/d2/98/c8/47/88/DM00477635/files/DM00477635.pdf/jcr:content/translations/en.DM00477635.pdf)): modifies the address of each bank in the memory map
* Swap does not need to happen immediately after update but could happen at a later time-for our application we probably want the swap to happen immediately
* Option-byte programming: setting OPTSTRT in FLASH\_NSCR(Pg.299, [STM32U5 Reference Manual](https://www.st.com/content/ccc/resource/technical/document/reference_manual/group0/f3/60/ca/d2/98/c8/47/88/DM00477635/files/DM00477635.pdf/jcr:content/translations/en.DM00477635.pdf))(Pg.307, [STM32U5 Reference Manual](https://www.st.com/content/ccc/resource/technical/document/reference_manual/group0/f3/60/ca/d2/98/c8/47/88/DM00477635/files/DM00477635.pdf/jcr:content/translations/en.DM00477635.pdf))
* Start every model with dual bank mode enabled then we only have to worry about switching the SWAP\_BANK bit
* Flash Registers boundaries: 0x4002 2000-0x4002 23FF (pg.147, [STM32U5 Reference Manual](https://www.st.com/content/ccc/resource/technical/document/reference_manual/group0/f3/60/ca/d2/98/c8/47/88/DM00477635/files/DM00477635.pdf/jcr:content/translations/en.DM00477635.pdf))
* FLASH\_OPTR = 0x4002 2040(pg. 361, [STM32U5 Reference Manual](https://www.st.com/content/ccc/resource/technical/document/reference_manual/group0/f3/60/ca/d2/98/c8/47/88/DM00477635/files/DM00477635.pdf/jcr:content/translations/en.DM00477635.pdf))
* Can use the Read Memory command to read the FLASH\_OPTR(read command, pg. 15, [USART protocol used in the STM32 bootloader](https://www.st.com/resource/en/application_note/an3155-usart-protocol-used-in-the-stm32-bootloader-stmicroelectronics.pdf))
* Use Write Memory command to write new bit for the SWAP\_BANK(Write command, pg. 20, [USART protocol used in the STM32 bootloader](https://www.st.com/resource/en/application_note/an3155-usart-protocol-used-in-the-stm32-bootloader-stmicroelectronics.pdf))
* STM32U5 always supports dual bank architecture, no need for setting up dual bank mode(pg. 2, [STM32U5 Flash Memory](https://www.st.com/content/ccc/resource/training/technical/product_training/group1/95/d2/99/8c/32/75/40/6b/STM32U5-Memory-FLASH_FLASH/files/STM32U5-Memory-FLASH_FLASH.pdf/_jcr_content/translations/en.STM32U5-Memory-FLASH_FLASH.pdf))
* STM32U5 is sent out by ST already in dual-bank mode(pg. 1, STM32U5\_FLASHOPTR.pdf)
* Reset will occur when a write happens on the FLASH\_OPTR register for the swap bank bit(pg. 20, [USART protocol used in the STM32 bootloader](https://www.st.com/resource/en/application_note/an3155-usart-protocol-used-in-the-stm32-bootloader-stmicroelectronics.pdf))

### **Procedure for Unlocking Registers to Write**

1. Unlock by writing KEY1 and KEY2 to FLASH\_SECKEYR or FLASH\_NSKEYR(pg. 298, [STM32U5 Reference Manual](https://www.st.com/content/ccc/resource/technical/document/reference_manual/group0/f3/60/ca/d2/98/c8/47/88/DM00477635/files/DM00477635.pdf/jcr:content/translations/en.DM00477635.pdf))
   The following sequence is used to unlock these registers:
   1. Write KEY1 = 0x45670123 in FLASH\_SECKEYR or FLASH\_NSKEYR.
   2. Write KEY2 = 0xCDEF89AB in FLASH\_SECKEYR or FLASH\_NSKEYR).
2. Write KEY1 and KEY2 to FLASH\_OPTKEYR(pg. 332, [STM32U5 Reference Manual](https://www.st.com/content/ccc/resource/technical/document/reference_manual/group0/f3/60/ca/d2/98/c8/47/88/DM00477635/files/DM00477635.pdf/jcr:content/translations/en.DM00477635.pdf))
   1. Unlock FLASH\_NSCR register with the LOCK clearing sequence (refer to Unlock the
   secure/nonsecure FLASH control registers).
   2. Write OPTKEY1 = 0x08192A3B in FLASH\_OPTKEYR.
   3. Write OPTKEY2 = 0x4C5D6E7F in FLASH\_OPTKEYR.
3. Write to desired register the new values

* This sequence will need to be used to swap memory banks
* Option-byte programming steps(pg. 307, [STM32U5 Reference Manual](https://www.st.com/content/ccc/resource/technical/document/reference_manual/group0/f3/60/ca/d2/98/c8/47/88/DM00477635/files/DM00477635.pdf/jcr:content/translations/en.DM00477635.pdf))
* Sequence to swap banks: Write KEY1 and KEY2 to FLASH\_NSKEYR -> Write OPTKEY1 and OPTKEY2 to FLASH\_OPTKEYR -> Write SWAP\_BANK to FLASH\_OPTR -> Write OPTSTRT to FLASH\_NSCR
* FLASH\_NSKEYR is to used to unlock FLASH\_NSCR which is used to allow nonsecure programming and erasing in flash(pg.331, [STM32U5 Reference Manual](https://www.st.com/content/ccc/resource/technical/document/reference_manual/group0/f3/60/ca/d2/98/c8/47/88/DM00477635/files/DM00477635.pdf/jcr:content/translations/en.DM00477635.pdf))
* OPSTRT is used start using the modifications to the option registers(pg.337, [STM32U5 Reference Manual](https://www.st.com/content/ccc/resource/technical/document/reference_manual/group0/f3/60/ca/d2/98/c8/47/88/DM00477635/files/DM00477635.pdf/jcr:content/translations/en.DM00477635.pdf))
* FLASH\_OPTKEYR is used to unlock the FLASH\_OPTR register(pg. 332, [STM32U5 Reference Manual](https://www.st.com/content/ccc/resource/technical/document/reference_manual/group0/f3/60/ca/d2/98/c8/47/88/DM00477635/files/DM00477635.pdf/jcr:content/translations/en.DM00477635.pdf))

### **Bank Erase Options**

* Bank1 or Bank2 mass erase options(pg. 299, [STM32U5 Reference Manual](https://www.st.com/content/ccc/resource/technical/document/reference_manual/group0/f3/60/ca/d2/98/c8/47/88/DM00477635/files/DM00477635.pdf/jcr:content/translations/en.DM00477635.pdf))





### **Possible Failure Points**

* Power Loss During Update: would be solved by querying the battery voltage before update
* Lossy Data During Update: small possibility that on the UART line data could be dropped or misinterpreted. This would be solved by sending a checksum with each byte which seems to already be the case with the STM32 bootloader commands.
* Bad/Corrupted Firmware: would be solved by using dual bank memory configuration and writing to the inactive bank, switching only when the bank is confirmed to have good firmware
* Storage: failure if we exceed 256kBytes, solved by limiting firmware size on developing end
* Device hanging during update: not sure if the particle or STM32 device would ever hang in some way during an update but I thought I would write it down since it came to mind

### **Tools**

* Use STM32CubeProgrammer to see memory blocks and registers in the device and to modify registers
* Use a modified version of the STM32 Flash Library made by particle since we are currently planning on make use of the particle asset ota feature.

 	--Mods will include changing address where the program will be flashed, changing to page erase instead of mass global erase, and adding switch bank logic to switch to 	new bank once verified



### **References**

[STM32U5 Reference Manual](https://www.st.com/content/ccc/resource/technical/document/reference_manual/group0/f3/60/ca/d2/98/c8/47/88/DM00477635/files/DM00477635.pdf/jcr:content/translations/en.DM00477635.pdf)

[On-the-fly firmware update for dual bank](https://www.st.com/content/ccc/resource/technical/document/application_note/group0/ab/6a/0f/b7/1a/84/40/c3/DM00230416/files/DM00230416.pdf/jcr:content/translations/en.DM00230416.pdf)

[USART protocol used in the STM32 bootloader](https://www.st.com/resource/en/application_note/an3155-usart-protocol-used-in-the-stm32-bootloader-stmicroelectronics.pdf)

[Introduction to system memory boot mode on STM32 MCUs](https://www.st.com/content/ccc/resource/technical/document/application_note/b9/9b/16/3a/12/1e/40/0c/CD00167594.pdf/files/CD00167594.pdf/jcr:content/translations/en.CD00167594.pdf)

[STM32U545 Datasheet](https://www.st.com/resource/en/datasheet/stm32u545ce.pdf)

[STM32U5 Flash Memory](https://www.st.com/content/ccc/resource/training/technical/product_training/group1/95/d2/99/8c/32/75/40/6b/STM32U5-Memory-FLASH_FLASH/files/STM32U5-Memory-FLASH_FLASH.pdf/_jcr_content/translations/en.STM32U5-Memory-FLASH_FLASH.pdf)

[STM32CubeProgrammer](https://www.st.com/content/ccc/resource/technical/document/user_manual/group0/76/3e/bd/0d/cf/4d/45/25/DM00403500/files/DM00403500.pdf/jcr:content/translations/en.DM00403500.pdf)

[Particle STM32 Flash Library](https://github.com/particle-iot/STM32_Flash/tree/main)

"src\STM32U5_FLASHOPTR.pdf"
