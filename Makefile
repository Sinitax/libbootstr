PREFIX ?= /usr/local
LIBDIR ?= /lib
INCLDIR ?= /include

all: build/libbootstr.so build/puny

clean:
	rm -rf build

build:
	mkdir build

build/puny: src/puny.c build/libbootstr.so | build
	$(CC) -o $@ $< -g -I include -L build -lunistring -lbootstr

build/libbootstr.so: src/bootstr.c include/bootstr.h | build
	$(CC) -o $@ $< -I include -fPIC -shared -lunistring

test/%.phony: test/%.in test/%.out
	@echo "> test $*"
	test "$(shell cat test/$*.in | ./build/puny -e)" = "$(shell cat test/$*.out)"
	test "$(shell cat test/$*.out | ./build/puny -d)" = "$(shell cat test/$*.in)"

test: build/puny test/puny-basic.phony

install:
	install -m644 include/bootstr.h -t "$(DESTDIR)$(PREFIX)$(INCLDIR)"
	install -m755 build/libbootstr.so -t "$(DESTDIR)$(PREFIX)$(LIBDIR)"

uninstall:
	rm -f "$(DESTDIR)$(PREFIX)$(INCLDIR)/bootstr.h"
	rm -f "$(DESTDIR)$(PREFIX)$(LIBDIR)/libbootstr.so"

.PHONY: all clean test install uninstall
