CC = gcc
AR = ar

CFLAGS = -std=c99 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS = -Iinclude

BUILD_DIR = build
LIBRARY = $(BUILD_DIR)/libgahshomar.a
TEST_PROGRAM = $(BUILD_DIR)/calendar_tests.exe
WINDOWS_APP_PROGRAM = $(BUILD_DIR)/gahshomar.exe
LINUX_APP_PROGRAM = $(BUILD_DIR)/gahshomar

CORE_OBJECTS = $(BUILD_DIR)/calendar.o
TEST_OBJECTS = $(BUILD_DIR)/test_calendar.o

UNAME_S := $(shell uname -s 2>/dev/null)

.PHONY: all test clean app

ifeq ($(OS),Windows_NT)
all: $(LIBRARY) $(TEST_PROGRAM) $(WINDOWS_APP_PROGRAM)
else ifeq ($(UNAME_S),Linux)
all: $(LIBRARY) $(TEST_PROGRAM) $(LINUX_APP_PROGRAM)
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
$(WINDOWS_APP_PROGRAM): src/windows.c $(CORE_OBJECTS) include/gahshomar/calendar.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -mwindows src/windows.c $(CORE_OBJECTS) -o $@ -lopengl32 -lgdi32 -luser32

app: $(WINDOWS_APP_PROGRAM)
else ifeq ($(UNAME_S),Linux)
LINUX_BASE_PACKAGES = x11 gl pangocairo
LINUX_BASE_CFLAGS = $(shell pkg-config --cflags $(LINUX_BASE_PACKAGES) 2>/dev/null)
LINUX_BASE_LIBS = $(shell pkg-config --libs $(LINUX_BASE_PACKAGES) 2>/dev/null)

WAYLAND_PROTOCOLS_DIR = $(shell pkg-config --variable=pkgdatadir wayland-protocols 2>/dev/null)
WAYLAND_XML = $(WAYLAND_PROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml
WAYLAND_AVAILABLE = $(shell pkg-config --exists wayland-client wayland-egl egl wayland-protocols 2>/dev/null && command -v wayland-scanner >/dev/null 2>&1 && test -f "$(WAYLAND_XML)" && echo yes)

ifeq ($(WAYLAND_AVAILABLE),yes)
LINUX_WAYLAND_CFLAGS = $(shell pkg-config --cflags wayland-client wayland-egl egl)
LINUX_WAYLAND_LIBS = $(shell pkg-config --libs wayland-client wayland-egl egl)
LINUX_WAYLAND_DEFINE = -DGH_HAVE_WAYLAND
LINUX_WAYLAND_SOURCE = $(BUILD_DIR)/xdg-shell-protocol.c
LINUX_WAYLAND_HEADER = $(BUILD_DIR)/xdg-shell-client-protocol.h

$(LINUX_WAYLAND_HEADER): $(WAYLAND_XML) | $(BUILD_DIR)
	wayland-scanner client-header $< $@

$(LINUX_WAYLAND_SOURCE): $(WAYLAND_XML) | $(BUILD_DIR)
	wayland-scanner private-code $< $@
endif

$(LINUX_APP_PROGRAM): src/linux.c $(CORE_OBJECTS) include/gahshomar/calendar.h $(LINUX_WAYLAND_SOURCE) $(LINUX_WAYLAND_HEADER)
	@pkg-config --exists $(LINUX_BASE_PACKAGES) || { echo "Missing Linux development packages: $(LINUX_BASE_PACKAGES)"; exit 1; }
	$(CC) $(CPPFLAGS) -I$(BUILD_DIR) $(CFLAGS) $(LINUX_WAYLAND_DEFINE) $(LINUX_BASE_CFLAGS) $(LINUX_WAYLAND_CFLAGS) src/linux.c $(LINUX_WAYLAND_SOURCE) $(CORE_OBJECTS) -o $@ $(LINUX_BASE_LIBS) $(LINUX_WAYLAND_LIBS)

app: $(LINUX_APP_PROGRAM)
else
app:
	@echo "No native desktop frontend is configured for this platform."
endif

test: $(TEST_PROGRAM)
	./$(TEST_PROGRAM)

clean:
	rm -rf $(BUILD_DIR)
