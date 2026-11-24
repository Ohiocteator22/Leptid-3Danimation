# Leptid — Makefile (Windows-first project; this Makefile targets Linux/macOS/g++,
# for Windows see README.md § Building on Windows)

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable
INCLUDES  = -Isrc -Ithird_party/stb
LDFLAGS  ?=
BIN      = bin

UNAME := $(shell uname)
# X11 backend (interactive GUI on Linux) — enabled automatically when available
X11_CFLAGS :=
X11_LIBS   :=
ifeq ($(UNAME),Linux)
  ifneq ($(shell pkg-config --exists x11 && echo yes),)
    X11_CFLAGS := -DLEPTID_HAS_X11 $(shell pkg-config --cflags x11)
    X11_LIBS   := $(shell pkg-config --libs x11)
  endif
endif

# Windows (MSYS2/MinGW): the Win32 backend is active under _WIN32; link the
# sockets lib used by the Ollama client's Winsock2 shim, and suppress the
# console window that would otherwise sit behind the GUI.
W32_LIBS    :=
W32_LDFLAGS :=
ifeq ($(findstring MINGW,$(UNAME)),MINGW)
  W32_LIBS    := -lws2_32
  W32_LDFLAGS := -mwindows
endif

APP_SRCS   = $(filter-out src/app/main.cpp,$(shell find src -name '*.cpp' | sort))
GUI_MAIN   = src/app/main.cpp
TEST_SRCS  = $(shell find tests -name '*.cpp' | sort)
FONTCPP    = $(BIN)/fontdata.cpp

.PHONY: all clean tests run run-tests demo

all: $(BIN)/leptid

$(BIN):
	mkdir -p $(BIN)

$(BIN)/bin2cpp: tools/bin2cpp.cpp | $(BIN)
	$(CXX) $(CXXFLAGS) -o $@ $<

$(FONTCPP): $(BIN)/bin2cpp third_party/fonts/DejaVuSans.ttf third_party/fonts/DejaVuSans-Bold.ttf | $(BIN)
	$(BIN)/bin2cpp third_party/fonts/DejaVuSans.ttf DejaVuSans_font > $@
	$(BIN)/bin2cpp third_party/fonts/DejaVuSans-Bold.ttf DejaVuSansBold_font >> $@

ASSETCPP = $(BIN)/assets.cpp
ASSETS := $(wildcard assets/brushes/*.png) $(wildcard assets/icons/*.png)

$(ASSETCPP): $(BIN)/bin2cpp $(ASSETS) | $(BIN)
	@: > $@
	@for f in assets/brushes/tip_*.png; do \
		sym=$$(basename $$f .png | sed 's/^tip_/tip_/'); \
		$(BIN)/bin2cpp $$f $${sym}_png >> $@; \
	done
	@for f in assets/icons/leptid_*.png; do \
		sym=$$(basename $$f .png | sed 's/-/_/g'); \
		$(BIN)/bin2cpp $$f $${sym}_png >> $@; \
	done

$(BIN)/leptid: $(APP_SRCS) $(GUI_MAIN) $(FONTCPP) $(ASSETCPP)
	@mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(X11_CFLAGS) -o $@ $(APP_SRCS) $(GUI_MAIN) $(FONTCPP) $(ASSETCPP) $(X11_LIBS) $(W32_LIBS) $(LDFLAGS) $(W32_LDFLAGS)

tests: $(BIN)/leptid_tests

ifneq ($(strip $(TEST_SRCS)),)
$(BIN)/leptid_tests: $(APP_SRCS) $(TEST_SRCS) $(FONTCPP) $(ASSETCPP)
	@mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(X11_CFLAGS) -o $@ $(APP_SRCS) $(TEST_SRCS) $(FONTCPP) $(ASSETCPP) $(X11_LIBS) $(W32_LIBS) $(LDFLAGS) $(W32_LDFLAGS)
endif

run: $(BIN)/leptid
	$(BIN)/leptid

run-tests: $(BIN)/leptid_tests
	$(BIN)/leptid_tests

demo: $(BIN)/leptid
	$(BIN)/leptid --demo demo/hello_leptid.lep --gif demo/hello.gif --mp4 demo/hello.mp4 --out demo/render

clean:
	rm -rf $(BIN) demo/render

# Single-file build (no display backend; headless-capable: --demo/--headless/--ui-shot)
# For a single Windows .exe see README "Single-file distribution".
static: $(FONTCPP) $(ASSETCPP)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -DLEPTID_STATIC_BUILD -o bin/leptid-static $(APP_SRCS) $(GUI_MAIN) $(FONTCPP) $(ASSETCPP) -static -static-libgcc -static-libstdc++ $(LDFLAGS)
