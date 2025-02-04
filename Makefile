VERSION ?= nightly
CFLAGS := -std=gnu99 -Wall -lmpv -lX11 -lm ./inih/ini.c ./flag/flag.c

build:
	gcc *.c -o camviewport $(CFLAGS) -O3 -s -DVERSION="\"$(VERSION)\""

debug:
	gcc *.c -o camviewport $(CFLAGS) -O0

workflow-build:
	sudo apt-get update -y 
	sudo apt-get install -y libmpv-dev
	gcc *.c -o camviewport $(CFLAGS) -O3 -s -DVERSION="\"$(VERSION)\""
	mkdir dist
	cp camviewport dist/camviewport_Linux_X86_64
	tar -czf dist/layouts.tar.gz layouts
