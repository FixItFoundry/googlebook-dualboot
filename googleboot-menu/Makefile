CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra
LIBS := -lm -lz

all: bin/googleboot-menu

bin/googleboot-menu: src/googleboot-menu.c src/google_sans_atlas.h
	mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

preview: bin/googleboot-menu
	./bin/googleboot-menu --theme theme/theme.conf --preview theme/preview-main.png
	./bin/googleboot-menu --theme theme/theme.conf --preview-luks --preview theme/preview-luks.png

clean:
	rm -f bin/googleboot-menu theme/preview-*.png

.PHONY: all preview clean
