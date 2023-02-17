PREFIX ?= /usr/local
BINDIR ?= /bin
LIBDIR ?= /lib

all: libbootstr.so puny

clean:
	rm -f puny

puny: puny.c libbootstr.so
	$(CC) -o $@ $(filter %.c,$^) -g -lunistring -L . -lbootstr

test/%.phony: test/%.in test/%.out
	@echo "test $*"
	test "$(shell cat test/$*.in | ./puny -e)" = "$(shell cat test/$*.out)"
	test "$(shell cat test/$*.out | ./puny -d)" = "$(shell cat test/$*.in)"

test: puny test/basic.phony

libbootstr.so: bootstr.o
	$(CC) -o $@ $^ -fPIC -shared -lunistring

install:
	install -m755 libbootstr.so -t "$(DESTDIR)$(PREFIX)$(LIBDIR)"
	install -m755 puny -t "$(DESTDIR)$(PREFIX)$(BINDIR)"

uninstall:
	rm -f "$(DESTDIR)$(PREFIX)$(LIBDIR)/libbootstr.so"
	rm -f "$(DESTDIR)$(PREFIX)$(BINDIR)/puny"

.PHONY: all clean test install uninstall
