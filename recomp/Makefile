# make -C recomp   (precisa de gcc ou clang; o C em ct2_funcs_*.c e gerado por tools/sh4c.py a partir do SEU 1ST_READ.BIN)
CC ?= gcc
SRCS = $(wildcard ct2_funcs_*.c) ct2_dispatch.c sh4rt.c
test_ct2: test_ct2.c $(SRCS) sh4rt.h ct2_funcs.h
	$(CC) -O1 -w -D_DEFAULT_SOURCE -I. test_ct2.c $(SRCS) -o $@ -lm
check: test_ct2
	./test_ct2 ../disc/1ST_READ.BIN scan_list.txt
