# 主机侧测试程序，编译时带上公共的 frame_io.c / iap_flow.c
# BINS 里的名字直接对应测试目的；新增测试时把目标名加进 BINS

CC     ?= cc
CFLAGS  = -std=gnu11 -Wall -Wextra -O2

BINS    = test_iap_basic test_do_set test_dma_wrap test_iap_upgrade

COMMON  = frame_io.c iap_flow.c
HEADERS = frame_io.h iap_flow.h

all: $(BINS)

%: %.c $(COMMON) $(HEADERS)
	$(CC) $(CFLAGS) $< $(COMMON) -o $@

clean:
	rm -f $(BINS) test_dma_overflow test_frame_errors

.PHONY: all clean
