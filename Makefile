VERSION ?= nightly
CFLAGS := -std=gnu99 -Wall -lmpv -lX11 -lm ./inih/ini.c ./flag/flag.c

build:
	mkdir -p dist
	gcc *.c -o dist/camviewport_$(shell uname)_$(shell uname -m) $(CFLAGS) -O3 -s -DVERSION="\"$(VERSION)\""

debug:
	mkdir -p dist
	gcc *.c -o dist/camviewport_$(shell uname)_$(shell uname -m) $(CFLAGS) -O0
