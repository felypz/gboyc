PROGRAM = gboyc
CC = cc
CFLAGS = -O2
LDFLAGS =
PREFIX = /usr/local
DESTDIR =

all:
	mkdir -p build
	{ echo 'const unsigned char LICENSE_TEXT[] = {'; \
	od -An -v -tx1 LICENSE | sed -e 's/^ *//' -e 's/ /,0x/g' -e 's/^/0x/' -e 's/$$/,/'; \
	echo '0};'; \
	echo 'const unsigned long LICENSE_SIZE = sizeof LICENSE_TEXT - 1;'; } > build/license_text.c
	$(CC) -std=c11 -Wall -Wextra -Wpedantic $(CFLAGS) `pkg-config --cflags sdl3` \
		src/*.c build/license_text.c $(LDFLAGS) -o $(PROGRAM) `pkg-config --libs sdl3`

debug:
	$(MAKE) CFLAGS="-O0 -g3 -fsanitize=address,undefined" \
		LDFLAGS="-fsanitize=address,undefined" all

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f $(PROGRAM) $(DESTDIR)$(PREFIX)/bin/
	chmod 755 $(DESTDIR)$(PREFIX)/bin/$(PROGRAM)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(PROGRAM)

clean:
	rm -rf build $(PROGRAM)

.PHONY: all debug install uninstall clean
