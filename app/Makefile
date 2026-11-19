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
CHIP     = GD32F330
DEVICE   = GD32F330CB

# dir
SRCDIR   = src
FW       = Firmware
RTOS     = FreeRTOS
RTT      = RTT

# all c files and .o .d files
RTOS_SRCS  = $(wildcard $(RTOS)/*.c)
CMSIS_SRCS = $(FW)/CMSIS/GD/GD32F3x0/Source/system_gd32f3x0.c \
             $(FW)/CMSIS/GD/GD32F3x0/Source/GCC/newlib/syscalls.c
STDP_SRCS  = $(wildcard $(FW)/GD32F3x0_standard_peripheral/Source/*.c)
RTT_SRCS   = $(wildcard $(RTT)/*.c)

SRCS = $(wildcard $(SRCDIR)/*.c) $(CMSIS_SRCS) $(STDP_SRCS) $(RTOS_SRCS) $(RTT_SRCS)
OBJS = $(patsubst %.c,$(OBJDIR)/%.o,$(SRCS)) $(OBJDIR)/startup_gd32f3x0.o $(OBJDIR)/SEGGER_RTT_ASM_ARMv7M.o
DEPS = $(patsubst %.c,$(OBJDIR)/%.d,$(SRCS))

# ================= 构建配置：make 默认 debug =================
BUILD_CONFIG := debug

ifeq ($(BUILD_CONFIG),debug)
	OBJDIR   := build/debug/obj
	OUTDIR   := build/debug
	EXE      := $(OUTDIR)/$(TARGET)_debug
	OPT      := $(if $(OPT),$(OPT),-Og) -g3 -gdwarf-4 -fno-omit-frame-pointer
	CFG_DEFS := -DDEBUG=1 -DUSE_FULL_ASSERT
else
	OBJDIR   := build/release/obj
	OUTDIR   := build/release
	EXE      := $(OUTDIR)/$(TARGET)
	OPT      := $(if $(OPT),$(OPT),-O2)
	CFG_DEFS := -DNDEBUG
endif

ARCH = -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
INC = -I$(SRCDIR) -I../common -I$(FW)/CMSIS -I$(FW)/CMSIS/GD/GD32F3x0/Include \
   -I$(FW)/GD32F3x0_standard_peripheral/Include -I$(RTOS)/include \
   -I$(RTT)/

CFLAGS  = -std=gnu11 $(ARCH) -D$(CHIP) -DVECT_TAB_OFFSET=0x7000U $(CFG_DEFS) $(OPT) \
          -Wall -ffunction-sections  -fno-strict-aliasing -fdata-sections $(INC)
LDFLAGS = $(ARCH) -T$(LDSCRIPT) -Wl,--gc-sections \
          -Wl,-Map=$(EXE).map --specs=nano.specs \
          -Wl,--defsym=__stack_size=0x400

# version section use
CFLAGS += -DFW_VER_MAJOR=$(VER_MAJOR) -DFW_VER_MINOR=$(VER_MINOR)
CFLAGS += -DFW_VERSION_STR=\"version$(VER_MAJOR).$(VER_MINOR)\"

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

FMT_SKIP := $(SRCDIR)/FreeRTOSConfig.h $(SRCDIR)/gd32f3x0_it.c $(SRCDIR)/gd32f3x0_it.h $(SRCDIR)/gd32f3x0_libopt.h
FMT_FILES := $(filter-out $(FMT_SKIP),$(wildcard $(SRCDIR)/*.c $(SRCDIR)/*.h))
format:
	@astyle -n --style=linux $(FMT_FILES)

clean:
	@rm -rf build

echo:
	@echo "SRCS: $(SRCS)"
	@echo "OBJS: $(OBJS)"
	@echo "DEPS: $(DEPS)"
	@echo "FORMAT: $(FMT_FILES)"

.PHONY: all debug release format clean echo
.DELETE_ON_ERROR:
