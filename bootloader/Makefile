# bootloader 独立构建：不依赖 FreeRTOS/RTT，也不链接 newlib
PREFIX   = arm-none-eabi-
CC       = $(PREFIX)gcc
OBJCOPY  = $(PREFIX)objcopy
SIZE     = $(PREFIX)size

TARGET   = boot
BUILD    = build
LDSCRIPT = boot_flash.ld

STARTUP  = Firmware/CMSIS/GD/GD32F3x0/Source/GCC/startup_gd32f3x0.S
SRCS     = src/boot.c \
           src/crc16.c \
           Firmware/CMSIS/GD/GD32F3x0/Source/system_gd32f3x0.c \
           Firmware/GD32F3x0_standard_peripheral/Source/gd32f3x0_misc.c

OBJS     = $(patsubst %.c,$(BUILD)/obj/%.o,$(SRCS)) \
           $(BUILD)/obj/startup_gd32f3x0.o

ARCH     = -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
INC      = -Isrc \
           -IFirmware/CMSIS \
           -IFirmware/CMSIS/GD/GD32F3x0/Include \
           -IFirmware/GD32F3x0_standard_peripheral/Include

CFLAGS   = -std=gnu11 $(ARCH) -DGD32F330 -Og -g3 \
           -Wall -ffunction-sections -fdata-sections -fno-strict-aliasing \
           -ffreestanding $(INC)
LDFLAGS  = $(ARCH) -T$(LDSCRIPT) -Wl,--gc-sections \
           -Wl,-Map=$(BUILD)/$(TARGET).map -nostdlib \
           -Wl,--start-group -lgcc -Wl,--end-group

all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).bin

$(BUILD)/obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/obj/startup_gd32f3x0.o: $(STARTUP)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJS) $(LDSCRIPT) Makefile
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	$(SIZE) $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -rf $(BUILD)

-include $(OBJS:.o=.d)

.PHONY: all clean
