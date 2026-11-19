# 主机侧测试程序，编译时带上公共的 frame_io.c / iap_flow.c
# BINS 里的名字直接对应测试目的；新增测试时把目标名加进 BINS

CC     ?= cc
CFLAGS  = -std=gnu11 -Wall -Wextra -O2

BUILD_DIR = build
BINS    = test_iap_basic test_do_set test_dma_wrap test_iap_upgrade test_boot_status
TARGETS = $(addprefix $(BUILD_DIR)/,$(BINS))

COMMON  = frame_io.c iap_flow.c
HEADERS = frame_io.h iap_flow.h

all: $(TARGETS)

$(BUILD_DIR)/%: %.c $(COMMON) $(HEADERS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $< $(COMMON) -o $@

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean
