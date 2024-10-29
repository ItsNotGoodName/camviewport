CFLAGS := -std=gnu99 -Wall -lmpv -lX11 -lm

debug:
	gcc *.c -o camviewport-debug $(CFLAGS) -O0

nightly:
	gcc *.c -o camviewport $(CFLAGS) -O3 -s -DVERSION="\"nightly\""
