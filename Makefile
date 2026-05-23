ROOT := $(abspath ..)
CC := $(ROOT)/tools/w64devkit/bin/gcc.exe
export PATH := $(ROOT)/tools/w64devkit/bin;$(PATH)
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -O2 -g -D_WIN32_WINNT=0x0601 -Iinclude
CORE := src/util.c src/reader.c src/fat.c src/exfat.c src/carve.c src/validate.c src/metadata.c src/engine.c
LIBS := -lcomctl32 -lcomdlg32 -lshell32 -lole32 -lgdiplus

.PHONY: all clean test portable

all: bin/recu-cli.exe bin/recu-classic.exe bin/mk_test_images.exe

bin:
	mkdir -p bin

bin/recu-cli.exe: $(CORE) src/cli.c include/recu.h | bin
	$(CC) $(CFLAGS) -o $@ $(CORE) src/cli.c

bin/recu-classic.exe: $(CORE) src/gui.c include/recu.h | bin
	$(CC) $(CFLAGS) -mwindows -o $@ $(CORE) src/gui.c $(LIBS)

bin/recu.exe: bin/recu-cli.exe | bin
	cp $< $@

bin/recu-gui.exe: bin/recu-classic.exe | bin
	cp $< $@

bin/mk_test_images.exe: tools/mk_test_images.c | bin
	$(CC) $(CFLAGS) -o $@ tools/mk_test_images.c

bin/synthetic_media_tests.exe: $(CORE) tools/synthetic_media_tests.c include/recu.h | bin
	$(CC) $(CFLAGS) -o $@ $(CORE) tools/synthetic_media_tests.c

test: bin/synthetic_media_tests.exe
	$<

portable: all
	powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/make_portable.ps1

clean:
	rm -rf bin
