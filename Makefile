# target and toolchain
TARGET   = fw
PREFIX   = arm-none-eabi-
CC       = $(PREFIX)gcc
OBJCOPY  = $(PREFIX)objcopy
SIZE     = $(PREFIX)size

# chip macro define
DEVICE   = GD32F310

# dir
SRCDIR   = src
FW       = Firmware
RTOS	 = FreeRTOS

# all c files and .o .d files
RTOS_SRCS = $(wildcard $(RTOS)/*.c)
CMSIS_SRCS = $(FW)/CMSIS/GD/GD32F3x0/Source/system_gd32f3x0.c \
		  $(FW)/CMSIS/GD/GD32F3x0/Source/GCC/newlib/syscalls.c
STDP_SRCS  = $(wildcard $(FW)/GD32F3x0_standard_peripheral/Source/*.c)

SRCS = $(wildcard $(SRCDIR)/*.c) $(CMSIS_SRCS) $(STDP_SRCS) $(RTOS_SRCS)
OBJS = $(patsubst %.c,$(OBJDIR)/%.o,$(SRCS)) $(OBJDIR)/startup_gd32f3x0.o
DEPS = $(patsubst %.c,$(OBJDIR)/%.d,$(SRCS))

# ================= 构建配置：make 默认 debug =================
BUILD_CONFIG := debug

ifeq ($(BUILD_CONFIG),debug)
	OBJDIR   := build/debug/obj
	OUTDIR   := build/debug
	EXE      := $(OUTDIR)/$(TARGET)_dbg
	OPT      := $(if $(OPT),$(OPT),-Og) -g3 -gdwarf-4 -fno-omit-frame-pointer
	CFG_DEFS := -DDEBUG=1 -DUSE_FULL_ASSERT
else
	OBJDIR   := build/release/obj
	OUTDIR   := build/release
	EXE      := $(OUTDIR)/$(TARGET)
	OPT      := $(if $(OPT),$(OPT),-O2) -DNDEBUG
	CFG_DEFS :=
endif

ARCH = -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
INC = -I$(SRCDIR) \
   -I$(FW)/CMSIS \
   -I$(FW)/CMSIS/GD/GD32F3x0/Include \
   -I$(FW)/GD32F3x0_standard_peripheral/Include \
   -I$(RTOS)/include

CFLAGS  = -std=gnu11 $(ARCH) -D$(DEVICE) $(CFG_DEFS) $(OPT) \
          -Wall -ffunction-sections -fdata-sections $(INC)
LDFLAGS = $(ARCH) -T$(LDSCRIPT) -Wl,--gc-sections \
          -Wl,-Map=$(EXE).map --specs=nano.specs \
          -Wl,--defsym=__stack_size=0x400

LDSCRIPT = $(FW)/CMSIS/GD/GD32F3x0/Source/GCC/Ld/gd32f310x8_flash.ld
STARTUP  = $(FW)/CMSIS/GD/GD32F3x0/Source/GCC/startup_gd32f3x0.S


all: $(EXE).elf $(EXE).bin

$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(OBJDIR)/startup_gd32f3x0.o: $(STARTUP)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(EXE).elf: $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	$(SIZE) $@

$(EXE).bin: $(EXE).elf
	$(OBJCOPY) -O binary $< $@

-include $(DEPS)

debug:
	$(MAKE) BUILD_CONFIG=debug

release:
	$(MAKE) BUILD_CONFIG=release

# 体积对比
compare: debug release
	chmod u+x ./compare.sh && bash ./compare.sh

FMT_SKIP := $(SRCDIR)/FreeRTOSConfig.h $(SRCDIR)/gd32f3x0_it.c $(SRCDIR)/gd32f3x0_it.h $(SRCDIR)/gd32f3x0_libopt.h
FMT_FILES := $(filter-out $(FMT_SKIP),$(wildcard $(SRCDIR)/*.c $(SRCDIR)/*.h))
format:
	@astyle -n --style=linux $(FMT_FILES)

flash: flash-rs

#TODO 使用probe-rs 烧录
flash-rs:

#TODO 使用GD32 ISP CONSOLE 烧录
flash-isp:

#TODO 使用 pyocd  烧录
flash-pyocd:


clean:                                                                                                                           
	@rm -rf build 

dbg:
	#TODO 使用probe-rs + gdb调试

echo:
	@echo "SRCS: $(SRCS)"
	@echo "OBJS: $(OBJS)"
	@echo "DEPS: $(DEPS)"

.PHONY: all debug release compare format flash flash-isp flash-pyocd clean dbg echo
.DELETE_ON_ERROR:
