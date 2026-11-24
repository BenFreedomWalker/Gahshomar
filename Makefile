CC = gcc
AR = ar

CFLAGS = -std=c99 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS = -Iinclude

ifeq ($(OS),Windows_NT)
    EXEEXT = .exe
    RM = rm -f
    RMDIR = rm -rf
else
    EXEEXT =
    RM = rm -f
    RMDIR = rm -rf
endif

BUILD_DIR = build
LIBRARY = $(BUILD_DIR)/libgahshomar.a
TEST_PROGRAM = $(BUILD_DIR)/calendar_tests$(EXEEXT)

CORE_OBJECTS = $(BUILD_DIR)/calendar.o
TEST_OBJECTS = $(BUILD_DIR)/test_calendar.o

.PHONY: all test clean

all: $(LIBRARY) $(TEST_PROGRAM)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/calendar.o: src/calendar.c include/gahshomar/calendar.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/test_calendar.o: tests/test_calendar.c include/gahshomar/calendar.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(LIBRARY): $(CORE_OBJECTS)
	$(AR) rcs $@ $^

$(TEST_PROGRAM): $(TEST_OBJECTS) $(LIBRARY)
	$(CC) $(CFLAGS) $(TEST_OBJECTS) $(LIBRARY) -o $@

test: $(TEST_PROGRAM)
	./$(TEST_PROGRAM)

clean:
	$(RMDIR) $(BUILD_DIR)
