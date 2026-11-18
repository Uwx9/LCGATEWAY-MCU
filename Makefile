# version (major.minor, u8)
VER_MAJOR = 0
VER_MINOR = 1

# target and toolchain
TARGET   = fw
PREFIX   = arm-none-eabi-
CC       = $(PREFIX)gcc
OBJCOPY  = $(PREFIX)objcopy
SIZE     = $(PREFIX)size

# chip macro define
# CHIP   = GD32F310
CHIP   = GD32F330
DEVICE = GD32F330CB

# dir
SRCDIR   = app/src
FW       = app/Firmware
RTOS	 = app/FreeRTOS
RTT	 = app/RTT

# all c files and .o .d files
RTOS_SRCS = $(wildcard $(RTOS)/*.c)
CMSIS_SRCS = $(FW)/CMSIS/GD/GD32F3x0/Source/system_gd32f3x0.c \
		  $(FW)/CMSIS/GD/GD32F3x0/Source/GCC/newlib/syscalls.c
STDP_SRCS  = $(wildcard $(FW)/GD32F3x0_standard_peripheral/Source/*.c)
RTT_SRCS = $(wildcard $(RTT)/*.c)

SRCS = $(wildcard $(SRCDIR)/*.c) $(CMSIS_SRCS) $(STDP_SRCS) $(RTOS_SRCS) $(RTT_SRCS)
OBJS = $(patsubst %.c,$(OBJDIR)/%.o,$(SRCS)) $(OBJDIR)/startup_gd32f3x0.o $(OBJDIR)/SEGGER_RTT_ASM_ARMv7M.o
DEPS = $(patsubst %.c,$(OBJDIR)/%.d,$(SRCS))

# ================= 构建配置：make 默认 debug =================
BUILD_CONFIG := debug

ifeq ($(BUILD_CONFIG),debug)
	OBJDIR   := app/build/debug/obj
	OUTDIR   := app/build/debug
	EXE      := $(OUTDIR)/$(TARGET)_debug
	OPT      := $(if $(OPT),$(OPT),-Og) -g3 -gdwarf-4 -fno-omit-frame-pointer
	CFG_DEFS := -DDEBUG=1 -DUSE_FULL_ASSERT
else
	OBJDIR   := app/build/release/obj
	OUTDIR   := app/build/release
	EXE      := $(OUTDIR)/$(TARGET)
	OPT      := $(if $(OPT),$(OPT),-O2)
	CFG_DEFS := -DNDEBUG
endif

ARCH = -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
INC = -I$(SRCDIR) -I$(FW)/CMSIS -I$(FW)/CMSIS/GD/GD32F3x0/Include \
   -I$(FW)/GD32F3x0_standard_peripheral/Include -I$(RTOS)/include \
   -I$(RTT)/

CFLAGS  = -std=gnu11 $(ARCH) -D$(CHIP) -DVECT_TAB_OFFSET=0x2000U $(CFG_DEFS) $(OPT) \
          -Wall -ffunction-sections  -fno-strict-aliasing -fdata-sections $(INC)
LDFLAGS = $(ARCH) -T$(LDSCRIPT) -Wl,--gc-sections \
          -Wl,-Map=$(EXE).map --specs=nano.specs \
          -Wl,--defsym=__stack_size=0x400

# version section use
CFLAGS += -DFW_VER_MAJOR=$(VER_MAJOR) -DFW_VER_MINOR=$(VER_MINOR)
CFLAGS += -DFW_VERSION_STR=\"version$(VER_MAJOR).$(VER_MINOR)\"

# LDSCRIPT = $(FW)/CMSIS/GD/GD32F3x0/Source/GCC/Ld/gd32f310x8_flash.ld
LDSCRIPT = $(FW)/CMSIS/GD/GD32F3x0/Source/GCC/Ld/gd32f330xB_flash.ld
STARTUP  = $(FW)/CMSIS/GD/GD32F3x0/Source/GCC/startup_gd32f3x0.S


all: $(EXE).elf $(EXE).bin

$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(OBJDIR)/startup_gd32f3x0.o: $(STARTUP)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
$(OBJDIR)/SEGGER_RTT_ASM_ARMv7M.o: $(RTT)/SEGGER_RTT_ASM_ARMv7M.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(EXE).elf: $(OBJS) $(LDSCRIPT) Makefile
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

#default build files
DEBUG_ELF_FILE = app/build/debug/$(TARGET)_debug.elf
DEBUG_BIN_FILE = app/build/debug/$(TARGET)_debug.bin
RELEASE_ELF_FILE = app/build/release/$(TARGET).elf
RELEASE_BIN_FILE = app/build/release/$(TARGET).bin

rtt: flash-rtt
run: flash reset

# 这里的 ELF 是让 probe-rs 从符号表里找到 _SEGGER_RTT 控制块的位置
attach:
	probe-rs attach --chip $(DEVICE) $(DEBUG_ELF_FILE)

# 只看有哪些 RTT 通道，不常驻
pipe: 
	probe-rs attach --chip $(DEVICE) --list-rtt $(DEBUG_ELF_FILE)

# download (chip not run, only download)
flash: flash-rs-debug-elf

# download and run rtt
flash-rtt: all
	probe-rs run --chip $(DEVICE) $(DEBUG_ELF_FILE)

reset:
	probe-rs reset --chip $(DEVICE)

# verify
verify:
	probe-rs verify --chip $(DEVICE) $(DEBUG_ELF_FILE)
erase:
	probe-rs erase  --chip $(DEVICE)


#TODO 使用probe-rs 烧录
# debug只多了断言, 烧录elf和bin差别不大
flash-rs-release-elf: all
	probe-rs download --chip $(DEVICE) $(RELEASE_ELF_FILE)
flash-rs-release-bin: all
	probe-rs download --chip $(DEVICE) --binary-format bin \
	--base-address 0x08002000 $(RELEASE_BIN_FILE)
flash-rs-debug-elf: all
	probe-rs download --chip $(DEVICE) $(DEBUG_ELF_FILE)
flash-rs-debug-bin: all
	probe-rs download --chip $(DEVICE) --binary-format bin \
	--base-address 0x08002000 $(DEBUG_BIN_FILE)

#TODO 使用GD32 ISP CONSOLE 烧录
flash-isp-release-elf:
flash-isp-release-bin:
flash-isp-debug-elf:
flash-isp-debug-bin:

#TODO 使用 pyocd  烧录
flash-pyocd-release-elf:
flash-pyocd-release-bin:
flash-pyocd-debug-elf:
flash-pyocd-debug-bin:

clean:                                                                                                                           
	@rm -rf app/build

dbg:
	#TODO 使用probe-rs + gdb调试

echo:
	@echo "SRCS: $(SRCS)"
	@echo "OBJS: $(OBJS)"
	@echo "DEPS: $(DEPS)"
	@echo "FORMAT: $(FMT_FILES)"

.PHONY: all debug release compare format run run-rtt run-nortt flash flash-rtt reset verify erase clean dbg echo
.DELETE_ON_ERROR:
