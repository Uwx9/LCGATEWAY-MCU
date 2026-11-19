DEVICE   = GD32F330CB

APP_ELF  = app/build/debug/fw_debug.elf
APP_BIN  = app/build/debug/fw_debug.bin
BOOT_ELF = bootloader/build/boot.elf
BOOT_BIN = bootloader/build/boot.bin

all: links app boot tests

links:
	@mkdir -p build app/build bootloader/build tests/build
	@ln -sfn ../app/build build/app
	@ln -sfn ../bootloader/build build/bootloader
	@ln -sfn ../tests/build build/tests

app: links
	$(MAKE) -C app

app-release: links
	$(MAKE) -C app release

boot: links
	$(MAKE) -C bootloader

tests: links
	$(MAKE) -C tests

# rtt 先擦除、烧 boot，再用 probe-rs run 烧 app 并输出 RTT
rtt: boot app
	probe-rs erase --chip $(DEVICE)
	probe-rs download --chip $(DEVICE) $(BOOT_ELF)
	probe-rs run --chip $(DEVICE) $(APP_ELF)

run: flash

# 整片擦除后烧 bootloader + app；元数据和 stage 也会被清空
flash: boot app
	probe-rs erase --chip $(DEVICE)
	probe-rs download --chip $(DEVICE) $(BOOT_ELF)
	probe-rs download --chip $(DEVICE) $(APP_ELF)
	probe-rs reset --chip $(DEVICE)

# 单独烧录，不擦除其他区域
fboot: flash-boot
flash-boot: boot
	probe-rs download --chip $(DEVICE) $(BOOT_ELF)
	probe-rs reset --chip $(DEVICE)

fapp: flash-app
flash-app: app
	probe-rs download --chip $(DEVICE) $(APP_ELF)
	probe-rs reset --chip $(DEVICE)

fbootbin: flash-boot-bin
flash-boot-bin: boot
	probe-rs download --chip $(DEVICE) --binary-format bin \
		--base-address 0x08000000 $(BOOT_BIN)
	probe-rs reset --chip $(DEVICE)
fappbin: flash-app-bin
flash-app-bin: app
	probe-rs download --chip $(DEVICE) --binary-format bin \
		--base-address 0x08007000 $(APP_BIN)
	probe-rs reset --chip $(DEVICE)

ferase: flash-erase
flash-erase:
	probe-rs erase --chip $(DEVICE)

reset:
	probe-rs reset --chip $(DEVICE)

verify: app
	probe-rs verify --chip $(DEVICE) $(APP_ELF)

verify-boot: boot
	probe-rs verify --chip $(DEVICE) $(BOOT_ELF)

attach: app
	probe-rs attach --chip $(DEVICE) $(APP_ELF)

pipe: app
	probe-rs attach --chip $(DEVICE) --list-rtt $(APP_ELF)

clean:
	$(MAKE) -C app clean
	$(MAKE) -C bootloader clean
	$(MAKE) -C tests clean

format:
	$(MAKE) -C app format

compare:
	$(MAKE) -C app debug
	$(MAKE) -C app release
	bash ./compare.sh

echo:
	$(MAKE) -C app echo

dbg:
	# TODO: probe-rs + gdb

.PHONY: all links app app-release boot tests rtt run flash flash-boot flash-app \
        flash-boot-bin flash-app-bin flash-erase reset verify verify-boot \
        attach pipe clean format compare echo dbg
