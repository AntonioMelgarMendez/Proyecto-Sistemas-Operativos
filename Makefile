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

.PHONY: check sanitize tsan
$(BUILD)/test-demo-fail: $(SOURCES) tests/fail_create.c $(wildcard include/*.h)
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) tests/fail_create.c -Wl,--wrap=pthread_create $(LDLIBS) -o $@

check: all test $(BUILD)/test-demo-fail
	python3 tests/test_integration.py $(BUILD)/monitor-demo
	python3 tests/test_terminal.py $(BUILD)/monitor-demo

sanitize:
	$(MAKE) BUILD=build/asan CFLAGS='-std=c17 -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer' check

tsan:
	$(MAKE) BUILD=build/tsan CFLAGS='-std=c17 -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=thread -fno-omit-frame-pointer' all test
	timeout 30s build/tsan/monitor-demo --plain --scenario normal --capacity 1 --items 10000 --delay 0 > build/tsan/stress.log

.PHONY: media paper
media: all
	python3 tools/create_demo.py

TECTONIC ?= tectonic
paper:
	$(TECTONIC) docs/paper/main.tex
