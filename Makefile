CC      ?= gcc
TARGET  := gboyc
SRCS    := $(wildcard src/*.c)
OBJS    := $(SRCS:src/%.c=build/%.o)

CFLAGS  := -std=c11 -Wall -Wextra -Wpedantic -O2 -g -MMD -MP $(shell pkg-config --cflags sdl3)
LDLIBS  := $(shell pkg-config --libs sdl3)

$(TARGET): $(OBJS)
	$(CC) $^ $(LDLIBS) -o $@

build/%.o: src/%.c
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build $(TARGET)

.PHONY: clean
-include $(OBJS:.o=.d)
