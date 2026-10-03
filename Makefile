CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic
PKG_CONFIG ?= pkg-config

# Set WINDOWS=1 to build a native x86_64 Windows executable.  The GTK4
# development files must be installed for the selected MinGW toolchain.
ifeq ($(WINDOWS),1)
WINDOWS_TRIPLE ?= x86_64-w64-mingw32
CC := $(WINDOWS_TRIPLE)-gcc
PKG_CONFIG := $(WINDOWS_TRIPLE)-pkg-config
WINDRES := $(WINDOWS_TRIPLE)-windres
EXE_SUFFIX := .exe
WINDOWS_LD := $(WINDOWS_TRIPLE)-ld
PLATFORM_LDFLAGS := -mwindows
WINDOWS_EXTRA_LIBS := -lshell32 -lole32

# quasi-msys2 keeps the UCRT64 GTK packages in a relocatable sysroot. Use the
# host pkg-config with target metadata when a cross pkg-config is unavailable.
QUASI_MSYS2_ROOT ?= $(HOME)/quasi-msys2/root
QUASI_MSYS2_PREFIX ?= ucrt64
WINDOWS_SYSROOT ?= $(QUASI_MSYS2_ROOT)
WINDOWS_PKGCONFIG_LIBDIR ?= $(WINDOWS_SYSROOT)/$(QUASI_MSYS2_PREFIX)/lib/pkgconfig:$(WINDOWS_SYSROOT)/$(QUASI_MSYS2_PREFIX)/share/pkgconfig
ifeq ($(shell command -v $(PKG_CONFIG) >/dev/null 2>&1 && echo yes),)
PKG_CONFIG := pkg-config
endif
export PKG_CONFIG_SYSROOT_DIR := $(WINDOWS_SYSROOT)
export PKG_CONFIG_LIBDIR := $(WINDOWS_PKGCONFIG_LIBDIR)
else
EXE_SUFFIX :=
WINDOWS_LD := ld
PLATFORM_LDFLAGS :=
WINDRES :=
WINDOWS_EXTRA_LIBS :=
WINDOWS_TRIPLE ?= x86_64-w64-mingw32
QUASI_MSYS2_ROOT ?= $(HOME)/quasi-msys2/root
QUASI_MSYS2_PREFIX ?= ucrt64
WINDOWS_SYSROOT ?= $(QUASI_MSYS2_ROOT)
endif

ifeq ($(WINDOWS),1)
ifeq ($(shell command -v $(CC) >/dev/null 2>&1 && echo yes),)
$(error Windows compiler '$(CC)' was not found; install a MinGW-w64 GTK4 toolchain or set CC)
endif
ifeq ($(shell command -v $(PKG_CONFIG) >/dev/null 2>&1 && echo yes),)
$(error pkg-config '$(PKG_CONFIG)' was not found)
endif
endif

GTK_CFLAGS := $(shell $(PKG_CONFIG) --cflags gtk4 | sed 's/-I/-isystem /g')
GTK_LIBS := $(shell $(PKG_CONFIG) --libs gtk4)
INCLUDES := -Isrc -Isrc/window

.PHONY: all windows run-windows clean run compile-commands app-image test test-windows log-capture-test-target bundle-windows run-bundle-windows vendor-windows nsis run-nsis-windows

BUILD_DIR := build
TARGET := $(BUILD_DIR)/finality-manager$(EXE_SUFFIX)
APPIMAGE_DIR := $(BUILD_DIR)/FinalityManager.AppDir
APPIMAGE := $(BUILD_DIR)/Finality-Manager-x86_64.AppImage
APPIMAGE_TOOL ?= appimagetool
WINDOW_SOURCES := src/window/main_window.c \
	src/window/playlists_window.c src/window/preferences_window.c \
	src/window/about_window.c src/window/logging_window.c
SOURCES := src/main.c src/app.c src/i18n.c src/config.c src/string_util.c \
	src/mods.c src/playlist.c src/win32_compat.c $(WINDOW_SOURCES)
WINDOW_HEADERS := src/window/main_window.h \
	src/window/playlists_window.h src/window/preferences_window.h \
	src/window/about_window.h src/window/logging_window.h
LOADER_OBJ := $(BUILD_DIR)/loader.o
ASSET_OBJS := $(BUILD_DIR)/aoh3.o $(BUILD_DIR)/aoh2de.o $(BUILD_DIR)/logo.o
I18N_OBJS := $(BUILD_DIR)/en-properties.o $(BUILD_DIR)/zh-CN-properties.o
ifeq ($(WINDOWS),1)
ICON_OBJ := $(BUILD_DIR)/logo-res.o
else
ICON_OBJ :=
endif

all: $(TARGET) compile-commands

# Build a portable Linux AppImage.  Set APPIMAGE_TOOL to an existing
# appimagetool binary when it is not available on PATH.
app-image: $(TARGET) res/Finality_Loader.jar res/logo.png res/aoh3.png res/aoh2de.png
	@if [ "$(WINDOWS)" = "1" ]; then echo "AppImage packaging requires the native Linux build (do not pass WINDOWS=1)." >&2; exit 1; fi
	rm -rf "$(APPIMAGE_DIR)"
	mkdir -p "$(APPIMAGE_DIR)/usr/bin" "$(APPIMAGE_DIR)/usr/share/applications" "$(APPIMAGE_DIR)/usr/share/icons/hicolor/256x256/apps"
	cp "$(TARGET)" "$(APPIMAGE_DIR)/usr/bin/finality-manager"
	mkdir -p "$(APPIMAGE_DIR)/usr/bin/res"
	cp res/Finality_Loader.jar res/aoh3.png res/aoh2de.png "$(APPIMAGE_DIR)/usr/bin/res/"
	cp res/logo.png "$(APPIMAGE_DIR)/usr/share/icons/hicolor/256x256/apps/finality-manager.png"
	printf '%s\n' '#!/bin/sh' 'exec "$$APPDIR/usr/bin/finality-manager" "$$@"' > "$(APPIMAGE_DIR)/AppRun"
	chmod +x "$(APPIMAGE_DIR)/AppRun"
	printf '%s\n' '[Desktop Entry]' 'Type=Application' 'Name=Finality Manager' 'Comment=Manage Finality game instances and mods' 'Exec=finality-manager' 'Icon=finality-manager' 'Terminal=false' 'Categories=Game;Utility;' > "$(APPIMAGE_DIR)/usr/share/applications/finality-manager.desktop"
	ln -sfn usr/share/applications/finality-manager.desktop "$(APPIMAGE_DIR)/finality-manager.desktop"
	ln -sfn usr/share/icons/hicolor/256x256/apps/finality-manager.png "$(APPIMAGE_DIR)/finality-manager.png"
	command -v "$(APPIMAGE_TOOL)" >/dev/null 2>&1 || { echo "$(APPIMAGE_TOOL) not found; install appimagetool or set APPIMAGE_TOOL=/path/to/appimagetool" >&2; exit 1; }
	"$(APPIMAGE_TOOL)" "$(APPIMAGE_DIR)" "$(APPIMAGE)"

windows:
	$(MAKE) WINDOWS=1 BUILD_DIR=build-windows all

# Build the Windows executable and copy it together with the GTK4 runtime into
# a self-contained directory that runs on a machine without MSYS2.
#
# The runtime DLLs and data are vendored in vendor/ucrt64, so bundling does not
# need an installed MSYS2.  Set WINDOWS_RUNTIME to use a different prefix (for
# example a live quasi-msys2 sysroot).
BUNDLE_DIR ?= build-windows/bundle
WINDOWS_PREFIX ?= $(WINDOWS_SYSROOT)/$(QUASI_MSYS2_PREFIX)
VENDOR_DIR ?= vendor/ucrt64
WINDOWS_RUNTIME ?= $(VENDOR_DIR)
bundle-windows:
	$(MAKE) WINDOWS=1 BUILD_DIR=build-windows all
	sh scripts/bundle-windows.sh \
		build-windows/finality-manager.exe "$(WINDOWS_RUNTIME)" res \
		"$(BUNDLE_DIR)" "$(WINDOWS_TRIPLE)-objdump"
	@echo "Bundled into $(BUNDLE_DIR)"

# Refresh the vendored runtime from a quasi-msys2 sysroot.  Only needed when
# upgrading the GTK4 version; the result is committed to the repository.
vendor-windows:
	sh scripts/vendor-windows.sh "$(WINDOWS_PREFIX)" "$(VENDOR_DIR)" \
		"$(WINDOWS_TRIPLE)-objdump"

# Build a Windows installer from the self-contained bundle with NSIS.  Needs
# the `makensis` tool (nsis package).  Output: build-windows/Finality-Manager-Setup.exe
APP_VERSION ?= 1.6.0
NSIS ?= makensis
NSIS_SCRIPT ?= installer/finality-manager.nsi
NSIS_OUTFILE ?= build-windows/Finality-Manager-Setup.exe
nsis: bundle-windows
	@command -v "$(NSIS)" >/dev/null 2>&1 || { \
		echo "$(NSIS) not found; install the nsis package" >&2; exit 1; }
	"$(NSIS)" -DAPP_VERSION="$(APP_VERSION)" \
		-DBUNDLE_DIR="$(CURDIR)/$(BUNDLE_DIR)" \
		-DOUTFILE="$(CURDIR)/$(NSIS_OUTFILE)" \
		-DAPP_ICON="$(CURDIR)/res/logo.ico" \
		"$(NSIS_SCRIPT)"
	@echo "Installer written to $(NSIS_OUTFILE)"

run-nsis-windows:
	WINEDEBUG=-all wine "$(NSIS_OUTFILE)"

run-bundle-windows:
	$(MAKE) WINDOWS=1 BUILD_DIR=build-windows bundle-windows
	cd "$(BUNDLE_DIR)" && WINEPATH="$(WINDOWS_RUNTIME)/bin" wine finality-manager.exe

run-windows:
	$(MAKE) WINDOWS=1 BUILD_DIR=build-windows all
	WINEPATH="$(WINDOWS_RUNTIME)/bin" wine build-windows/finality-manager.exe

$(BUILD_DIR)/en-properties.o: src/en.properties
	mkdir -p $(BUILD_DIR)
	$(WINDOWS_LD) -r -b binary -o $@ $<

$(BUILD_DIR)/zh-CN-properties.o: src/zh-CN.properties
	mkdir -p $(BUILD_DIR)
	$(WINDOWS_LD) -r -b binary -o $@ $<

compile-commands:
	@mkdir -p $(BUILD_DIR)
	@printf '[{"directory":"%s","command":"%s %s %s %s -o %s %s %s %s %s %s %s","file":"%s/src/main.c"}]\n' \
		"$(CURDIR)" "$(CC)" "$(CFLAGS)" "$(INCLUDES)" "$(GTK_CFLAGS)" "$(TARGET)" "$(SOURCES)" "$(LOADER_OBJ)" "$(I18N_OBJS)" "$(ASSET_OBJS)" "$(ICON_OBJ)" "$(GTK_LIBS) $(WINDOWS_EXTRA_LIBS)" "$(CURDIR)" > compile_commands.json

$(TARGET): $(SOURCES) src/app.h src/i18n.h src/config.h src/app_types.h \
	src/mods.h src/playlist.h src/string_util.h src/win32_compat.h \
	$(WINDOW_HEADERS) \
	$(LOADER_OBJ) $(I18N_OBJS) $(ASSET_OBJS) $(ICON_OBJ)
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) $(GTK_CFLAGS) $(SOURCES) $(LOADER_OBJ) $(I18N_OBJS) $(ASSET_OBJS) $(ICON_OBJ) -o $@ $(LDFLAGS) $(PLATFORM_LDFLAGS) $(GTK_LIBS) $(WINDOWS_EXTRA_LIBS)

$(BUILD_DIR)/aoh3.o: res/aoh3.png
	mkdir -p $(BUILD_DIR)
	$(WINDOWS_LD) -r -b binary -o $@ $<

$(BUILD_DIR)/aoh2de.o: res/aoh2de.png
	mkdir -p $(BUILD_DIR)
	$(WINDOWS_LD) -r -b binary -o $@ $<

$(BUILD_DIR)/logo.o: res/logo.png
	mkdir -p $(BUILD_DIR)
	$(WINDOWS_LD) -r -b binary -o $@ $<

$(BUILD_DIR)/logo-res.o: src/logo.rc res/logo.ico
	mkdir -p $(BUILD_DIR)
	$(WINDRES) -i $< -o $@

$(LOADER_OBJ): res/Finality_Loader.jar
	mkdir -p $(BUILD_DIR)
	$(WINDOWS_LD) -r -b binary -o $@ $<

run: $(TARGET)
	./$(TARGET)

# Headless tests for mod discovery, ModsOff.txt and playlists.
TEST_SOURCES := tests/playlist_test.c src/mods.c src/playlist.c src/i18n.c
$(BUILD_DIR)/playlist-test: $(TEST_SOURCES) src/mods.h src/playlist.h \
	src/app_types.h $(I18N_OBJS)
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) $(GTK_CFLAGS) $(TEST_SOURCES) $(I18N_OBJS) \
		-o $@ $(LDFLAGS) $(GTK_LIBS)

test: $(BUILD_DIR)/playlist-test
	./$(BUILD_DIR)/playlist-test

# Regression test for Windows Java log capture.  It must be a GUI-subsystem
# executable (like the application) to reproduce the missing-console case.
LOG_CAPTURE_TEST_SOURCES := tests/log_capture_test.c src/win32_compat.c
$(BUILD_DIR)/log-capture-test$(EXE_SUFFIX): $(LOG_CAPTURE_TEST_SOURCES) \
	src/win32_compat.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) $(GTK_CFLAGS) $(LOG_CAPTURE_TEST_SOURCES) \
		-o $@ $(LDFLAGS) $(PLATFORM_LDFLAGS) $(GTK_LIBS) $(WINDOWS_EXTRA_LIBS)

log-capture-test-target: $(BUILD_DIR)/log-capture-test$(EXE_SUFFIX)

# Run the Java log-capture regression test with the Windows build.  Requires
# Java to be visible to the Wine/Windows environment; set FINALITY_TEST_JAVA to
# the java.exe path to override.  Reports SKIP (and passes) when Java is absent.
# Runs against the vendored runtime so a missing gspawn helper is caught too.
test-windows:
	$(MAKE) WINDOWS=1 BUILD_DIR=build-windows log-capture-test-target
	@if command -v wine >/dev/null 2>&1 && [ "$$(uname -s)" != "Windows_NT" ]; then \
		cd build-windows && WINEPATH="$(CURDIR)/$(WINDOWS_RUNTIME)/bin" \
			wine log-capture-test.exe $(FINALITY_TEST_JAVA); \
	else \
		./build-windows/log-capture-test.exe $(FINALITY_TEST_JAVA); \
	fi

clean:
	rm -rf build build-windows
