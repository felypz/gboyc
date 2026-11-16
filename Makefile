CC      ?= gcc
TARGET  := gboyc
SRCS    := $(wildcard src/*.c)
OBJS    := $(SRCS:src/%.c=build/%.o)

CFLAGS  := -std=c11 -Wall -Wextra -Wpedantic -O2 -g -MMD -MP $(shell pkg-config --cflags sdl3)
LDLIBS  := $(shell pkg-config --libs sdl3)

all: $(TARGET) | config/boot

$(TARGET): $(OBJS)
	$(CC) $^ $(LDLIBS) -o $@

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build config/boot:
	mkdir -p $@

format:
	clang-format -i src/*.c src/*.h

check-format:
	clang-format --dry-run --Werror src/*.c src/*.h

clean:
	rm -rf build $(TARGET)

.PHONY: all format check-format clean
-include $(OBJS:.o=.d)
