# Makefile for the custom_scanner SANE backend (virtual scanner).
#
#   make                 build libsane-custom_scanner.so.1
#   make test            build and run the self test (no scanner needed)
#   make samples         generate the sample image set (needs ImageMagick)
#   sudo make install    install the backend, its configuration and the images
#   sudo make uninstall  remove everything that "make install" copied
#   make deb             build a Debian/Ubuntu package (needs dpkg-deb)
#   make clean
#
# Useful variables:
#   PREFIX=/usr/local     installation prefix
#   DESTDIR=/tmp/pkgroot  staging directory (used by packaging)
#   IMAGE_DIR=...         compiled-in default image directory
#   VERSION=1.2.3         version reported by "make deb"

BACKEND   := custom_scanner
LIB       := libsane-$(BACKEND).so.1
SRC       := custom_scanner.c
BUILD_DIR := build

VERSION   ?= 1.0.0
PREFIX    ?= /usr
CONF_DIR  ?= /etc/sane.d
SHARE_DIR ?= $(PREFIX)/share/$(BACKEND)
IMAGE_DIR ?= $(SHARE_DIR)/images

MULTIARCH := $(shell dpkg-architecture -qDEB_HOST_MULTIARCH 2>/dev/null || gcc -print-multiarch 2>/dev/null)
ifeq ($(strip $(MULTIARCH)),)
  ifneq ($(wildcard $(PREFIX)/lib64),)
    SANE_LIB_DIR ?= $(PREFIX)/lib64/sane
  else
    SANE_LIB_DIR ?= $(PREFIX)/lib/sane
  endif
else
  SANE_LIB_DIR ?= $(PREFIX)/lib/$(MULTIARCH)/sane
endif

CC       ?= cc
CFLAGS   ?= -O2 -g
CFLAGS   += -fPIC -std=gnu99 -Wall -Wextra
CFLAGS   += -DCUSTOM_SCANNER_DEFAULT_DIR=\"$(IMAGE_DIR)\"
LDLIBS   ?= -lsane

.PHONY: all test samples install uninstall deb clean

all: $(LIB)

$(LIB): $(SRC)
	$(CC) $(CFLAGS) -shared -o $@ $< $(LDFLAGS) $(LDLIBS)

$(BUILD_DIR)/selftest: tests/selftest.c $(SRC)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ tests/selftest.c $(SRC) $(LDLIBS)

test: $(BUILD_DIR)/selftest
	$(BUILD_DIR)/selftest

samples:
	./scripts/make_samples.sh

install: $(LIB)
	install -d "$(DESTDIR)$(SANE_LIB_DIR)"
	install -m 755 $(LIB) "$(DESTDIR)$(SANE_LIB_DIR)/$(LIB)"
	install -d "$(DESTDIR)$(CONF_DIR)"
	@if [ -f "$(DESTDIR)$(CONF_DIR)/$(BACKEND).conf" ]; then \
		echo "keeping the existing $(CONF_DIR)/$(BACKEND).conf"; \
	else \
		install -m 644 $(BACKEND).conf "$(DESTDIR)$(CONF_DIR)/$(BACKEND).conf"; \
	fi
	install -d "$(DESTDIR)$(SHARE_DIR)/images"
	@if ls samples/*.pnm >/dev/null 2>&1; then \
		cp -f samples/*.pnm "$(DESTDIR)$(SHARE_DIR)/images/"; \
		echo "installed $$(ls samples/*.pnm | wc -l) sample image(s)"; \
	elif [ -f test.pnm ]; then \
		install -m 644 test.pnm "$(DESTDIR)$(SHARE_DIR)/images/test.pnm"; \
		echo "installed test.pnm as the single fallback image (run 'make samples' for the full set)"; \
	fi
	@if [ -d "$(DESTDIR)$(CONF_DIR)/dll.d" ]; then \
		echo "$(BACKEND)" > "$(DESTDIR)$(CONF_DIR)/dll.d/$(BACKEND).conf"; \
		echo "enabled the backend through $(CONF_DIR)/dll.d/$(BACKEND).conf"; \
	elif [ -f "$(DESTDIR)$(CONF_DIR)/dll.conf" ]; then \
		grep -qxF "$(BACKEND)" "$(DESTDIR)$(CONF_DIR)/dll.conf" || echo "$(BACKEND)" >> "$(DESTDIR)$(CONF_DIR)/dll.conf"; \
		echo "enabled the backend in $(CONF_DIR)/dll.conf"; \
	else \
		echo "$(BACKEND)" > "$(DESTDIR)$(CONF_DIR)/dll.conf"; \
		echo "created $(CONF_DIR)/dll.conf"; \
	fi
	@echo ""
	@echo "Installed. Test it with:"
	@echo "  scanimage -L"
	@echo "  scanimage -d $(BACKEND):virtual > out.pnm"

uninstall:
	rm -f "$(DESTDIR)$(SANE_LIB_DIR)/$(LIB)"
	rm -f "$(DESTDIR)$(CONF_DIR)/$(BACKEND).conf"
	rm -f "$(DESTDIR)$(CONF_DIR)/dll.d/$(BACKEND).conf"
	@if [ -f "$(DESTDIR)$(CONF_DIR)/dll.conf" ]; then \
		grep -vxF "$(BACKEND)" "$(DESTDIR)$(CONF_DIR)/dll.conf" > "$(DESTDIR)$(CONF_DIR)/dll.conf.tmp" || true; \
		mv "$(DESTDIR)$(CONF_DIR)/dll.conf.tmp" "$(DESTDIR)$(CONF_DIR)/dll.conf"; \
	fi
	rm -rf "$(DESTDIR)$(SHARE_DIR)"

deb: $(LIB)
	./scripts/make_deb.sh "$(VERSION)"

clean:
	rm -f $(LIB)
	rm -rf $(BUILD_DIR)
