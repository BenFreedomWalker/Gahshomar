CC = gcc
AR = ar

CFLAGS = -std=c99 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS = -Iinclude

BUILD_DIR = build
LIBRARY = $(BUILD_DIR)/libgahshomar.a
TEST_PROGRAM = $(BUILD_DIR)/calendar_tests.exe
APP_PROGRAM = $(BUILD_DIR)/gahshomar.exe

CORE_OBJECTS = $(BUILD_DIR)/calendar.o
TEST_OBJECTS = $(BUILD_DIR)/test_calendar.o

.PHONY: all test clean app

ifeq ($(OS),Windows_NT)
all: $(LIBRARY) $(TEST_PROGRAM) $(APP_PROGRAM)
else
all: $(LIBRARY) $(TEST_PROGRAM)
endif

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/calendar.o: src/calendar.c include/gahshomar/calendar.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/calendar.c -o $@

$(BUILD_DIR)/test_calendar.o: tests/test_calendar.c include/gahshomar/calendar.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c tests/test_calendar.c -o $@

$(LIBRARY): $(CORE_OBJECTS)
	$(AR) rcs $@ $(CORE_OBJECTS)

$(TEST_PROGRAM): $(TEST_OBJECTS) $(LIBRARY)
	$(CC) $(CFLAGS) $(TEST_OBJECTS) $(LIBRARY) -o $@

ifeq ($(OS),Windows_NT)
$(APP_PROGRAM): src/windows.c $(CORE_OBJECTS) include/gahshomar/calendar.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -mwindows src/windows.c $(CORE_OBJECTS) -o $@ -lopengl32 -lgdi32 -luser32

app: $(APP_PROGRAM)
else
app:
	@echo "The native desktop frontend is currently implemented for Windows only."
endif

test: $(TEST_PROGRAM)
	./$(TEST_PROGRAM)

clean:
	rm -rf $(BUILD_DIR)
