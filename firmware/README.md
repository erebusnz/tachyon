# Firmware

## Flashing

Hold down the BOOT0 (B0) button and connect to the computer via USB, release after 1s. You should now be in DFU mode over USB.

You can test connection with:
```
STM32_Programmer_CLI --connect port=USB1
```

Flashing firmware:
```
STM32_Programmer_CLI --connect port=USB1 --download build\firmware.elf 0x08000000 --start
```
