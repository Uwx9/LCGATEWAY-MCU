# 主机侧测试程序，编译时带上公共的 frame_io.c / iap_flow.c
# 写完 test2..test4 后把它们加进 BINS 即可

CC     ?= cc
CFLAGS  = -std=gnu11 -Wall -Wextra -O2

BINS    = test1

COMMON  = frame_io.c iap_flow.c
HEADERS = frame_io.h iap_flow.h

all: $(BINS)

%: %.c $(COMMON) $(HEADERS)
	$(CC) $(CFLAGS) $< $(COMMON) -o $@

clean:
	rm -f test1 test2 test3 test4

.PHONY: all clean
