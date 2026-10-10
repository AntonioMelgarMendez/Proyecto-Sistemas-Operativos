CC = cc
CPPFLAGS = -D_POSIX_C_SOURCE=200809L -Iinclude
CFLAGS = -std=c17 -O2 -g -Wall -Wextra -Wpedantic -Werror
LDLIBS = -pthread
BUILD ?= build
SOURCES = $(wildcard src/*.c)
OBJECTS = $(patsubst src/%.c,$(BUILD)/%.o,$(SOURCES))

.PHONY: all clean test
all: $(BUILD)/monitor-demo

$(BUILD)/monitor-demo: $(OBJECTS)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BUILD)/%.o: src/%.c
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread -MMD -MP -c $< -o $@

clean:
	rm -rf build

-include $(OBJECTS:.o=.d)

$(BUILD)/test-monitor: tests/test_monitor.c src/monitor.c include/monitor.h
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_monitor.c src/monitor.c $(LDLIBS) -o $@

test: $(BUILD)/test-monitor
	timeout 15s $(BUILD)/test-monitor
