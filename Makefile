CC ?= gcc
CFLAGS ?= -Wall -Wextra -std=c11 -g
BUILD_DIR := build

.PHONY: test clean

test: $(BUILD_DIR)/test_frame
	$(BUILD_DIR)/test_frame

$(BUILD_DIR)/test_frame: tests/test_frame.c src/frame.c src/frame.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_frame.c src/frame.c

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)
