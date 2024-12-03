CFLAGS := -std=gnu99 -Wall -lmpv -lX11 -lm ./inih/ini.c ./flag/flag.c

debug:
	gcc *.c -o camviewport $(CFLAGS) -O0

nightly:
	gcc *.c -o camviewport $(CFLAGS) -O3 -s -DVERSION="\"nightly\""
