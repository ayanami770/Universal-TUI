# Universal TUI — dependency-free menuconfig-style TUI engine (C99).

CC      ?= cc
CFLAGS  ?= -std=c99 -O2 -Wall -Wextra -pedantic
MINGW   ?= x86_64-w64-mingw32-gcc

all: libutui.a demo

# Static library (link into your own program alongside utui.h).
libutui.a: utui.o
	$(AR) rcs $@ $^

utui.o: utui.c utui.h
	$(CC) $(CFLAGS) -c utui.c -o $@

# Example program.
demo: examples/demo.c utui.c utui.h
	$(CC) $(CFLAGS) -I. -o demo examples/demo.c utui.c

# Pure-Windows demo (MinGW cross). MSVC: cl /std:c11 /I. examples\demo.c utui.c
windows demo.exe: examples/demo.c utui.c utui.h
	$(MINGW) $(CFLAGS) -I. -o demo.exe examples/demo.c utui.c

asan: examples/demo.c utui.c utui.h
	$(CC) -std=c99 -g -O1 -fsanitize=address,undefined -Wall -Wextra -pedantic \
	  -I. -o demo-asan examples/demo.c utui.c

test: demo
	python3 tests/test_demo.py ./demo
	$(CC) $(CFLAGS) -I. -o test-config tests/test_config.c utui.c
	./test-config
	$(CC) $(CFLAGS) -I. -o test-utf8 tests/test_utf8.c
	./test-utf8
	$(CC) $(CFLAGS) -pthread -I. -o test-embed tests/test_embed.c utui.c
	./test-embed

clean:
	rm -f *.o *.a demo demo-asan demo.exe test-config test-utf8 test-embed test.config test-embed.config

.PHONY: all windows asan test clean
