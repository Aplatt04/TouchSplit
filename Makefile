# TouchSplit build
#   make            -> TouchSplit.exe
#   make installer  -> dist/TouchSplit-Setup-<ver>.exe   (needs NSIS: apt install nsis)
#   make dist       -> exe + installer + portable zip + SHA256SUMS.txt in dist/
#
# Cross-compiles from Linux/macOS/WSL with mingw-w64. Inside MSYS2/w64devkit on
# Windows use:  make CROSS=
CROSS    ?= x86_64-w64-mingw32-
CC       := $(CROSS)gcc
WINDRES  := $(CROSS)windres
MAKENSIS ?= makensis
CFLAGS   ?= -O2 -Wall -Wextra
LDFLAGS  := -municode -mwindows -static
LIBS     := -luser32 -lshell32 -lgdi32 -ladvapi32
VERSION  := $(shell sed -n 's/^\#define TS_VERSION_STR *"\(.*\)"/\1/p' src/version.h)
REPO_URL ?=

TouchSplit.exe: src/touchsplit.c src/version.h build/res.o
	$(CC) $(CFLAGS) $(LDFLAGS) src/touchsplit.c build/res.o -o $@ $(LIBS)

build/res.o: res/touchsplit.rc res/on.ico res/off.ico src/version.h
	mkdir -p build
	$(WINDRES) -I res res/touchsplit.rc -O coff -o $@

installer: dist/TouchSplit-Setup-$(VERSION).exe

dist/TouchSplit-Setup-$(VERSION).exe: TouchSplit.exe installer/touchsplit.nsi LICENSE docs/README.txt
	mkdir -p dist
	cd installer && $(MAKENSIS) -V2 -DVERSION=$(VERSION) $(if $(REPO_URL),-DREPO_URL=$(REPO_URL)) touchsplit.nsi

dist/TouchSplit-$(VERSION)-portable.zip: TouchSplit.exe LICENSE docs/README.txt
	mkdir -p dist/stage/TouchSplit
	cp TouchSplit.exe dist/stage/TouchSplit/
	cp LICENSE dist/stage/TouchSplit/LICENSE.txt
	cp docs/README.txt dist/stage/TouchSplit/README.txt
	cd dist/stage && rm -f ../$(notdir $@) && zip -qr ../$(notdir $@) TouchSplit
	rm -rf dist/stage

dist: installer dist/TouchSplit-$(VERSION)-portable.zip
	cp TouchSplit.exe dist/TouchSplit.exe
	cd dist && sha256sum TouchSplit-Setup-$(VERSION).exe TouchSplit-$(VERSION)-portable.zip TouchSplit.exe > SHA256SUMS.txt
	@cat dist/SHA256SUMS.txt

version:
	@echo $(VERSION)

clean:
	rm -rf build dist TouchSplit.exe

.PHONY: installer dist version clean
