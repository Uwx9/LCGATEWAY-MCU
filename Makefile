TARGET   = fw
PREFIX   = arm-none-eabi-
CC       = $(PREFIX)gcc
OBJCOPY  = $(PREFIX)objcopy
SIZE     = $(PREFIX)size

SRCDIR   = src
OBJDIR   = obj
FW       = Firmware
RTOS	 = FreeRTOS

# chip macro define
DEVICE   = GD32F310

LDSCRIPT = $(FW)/CMSIS/GD/GD32F3x0/Source/GCC/Ld/gd32f310x8_flash.ld
STARTUP  = $(FW)/CMSIS/GD/GD32F3x0/Source/GCC/startup_gd32f3x0.S

# all c files and .o .d files
RTOS_SRCS = $(wildcard $(RTOS)/*.c)
CMSIS_SRCS = $(FW)/CMSIS/GD/GD32F3x0/Source/system_gd32f3x0.c \
		  $(FW)/CMSIS/GD/GD32F3x0/Source/GCC/newlib/syscalls.c
STDP_SRCS  = $(wildcard $(FW)/GD32F3x0_standard_peripheral/Source/*.c)

SRCS       = $(wildcard $(SRCDIR)/*.c) $(CMSIS_SRCS) $(STDP_SRCS) $(RTOS_SRCS)
OBJS       = $(patsubst %.c,$(OBJDIR)/%.o,$(SRCS)) $(OBJDIR)/startup_gd32f3x0.o
DEPS       = $(patsubst %.c,$(OBJDIR)/%.d,$(SRCS))

INC = -I$(SRCDIR) \
   -I$(FW)/CMSIS \
   -I$(FW)/CMSIS/GD/GD32F3x0/Include \
   -I$(FW)/GD32F3x0_standard_peripheral/Include \
   -I$(RTOS)/include

CFLAGS = -std=gnu11 -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 \
	   -D$(DEVICE) -Wall -O2 -ffunction-sections -fdata-sections $(INC)
LDFLAGS = -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 \
          -T$(LDSCRIPT) -Wl,--gc-sections -Wl,-Map=$(TARGET).map --specs=nano.specs \
          -Wl,--defsym=__stack_size=0x400


all: $(TARGET).elf $(TARGET).bin

$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(OBJDIR)/startup_gd32f3x0.o: $(STARTUP)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	$(SIZE) $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

-include $(DEPS)

format:
	@astyle --style=linux src/main.c

flash: flash-pyocd

flash-isp:
	#TODO 使用GD32 ISP CONSOLE 烧录

flash-pyocd:
	#TODO 使用 pyocd  烧录
	
clean:                                                                                                                           
	@rm -rf $(OBJDIR) $(TARGET).elf $(TARGET).bin $(TARGET).map

debug:
	#TODO 使用pyocd + gdb调试

echo:
	@echo "SRCS: $(SRCS)"
	@echo "OBJS: $(OBJS)"
	@echo "DEPS: $(DEPS)"

.PHONY: all format flash flash-isp flash-pyocd clean debug echo
