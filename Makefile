# diskperf makefile

ROOT := ..
DSPF_DIR := $(ROOT)/diskperf
FFBASE_DIR := $(ROOT)/ffbase
FFOS_DIR := $(ROOT)/ffos

include $(FFBASE_DIR)/test/makeconf

BIN := diskperf
ifeq "$(OS)" "windows"
	BIN := diskperf.exe
endif

CFLAGS := -I$(DSPF_DIR)/src -I$(FFOS_DIR) -I$(FFBASE_DIR) \
	-DFFBASE_HAVE_FFERR_STR
ifeq "$(OPT)" "0"
	CFLAGS += -g -O0 -DFF_DEBUG
else
	CFLAGS += -O3 -s
	CFLAGS += -march=native
endif

# build, install
default: $(BIN)
	$(MAKE) -f $(firstword $(MAKEFILE_LIST)) install

# build, install, package
build-package: default
	$(MAKE) -f $(firstword $(MAKEFILE_LIST)) package

%.o: $(DSPF_DIR)/src/%.c \
		$(wildcard $(DSPF_DIR)/src/*.h) \
		$(wildcard $(DSPF_DIR)/src/util/*.h)
	$(C) $(CFLAGS) $< -o $@
$(BIN): main.o
	$(LINK) $+ $(LINKFLAGS) -o $@

test: test-stream.o
	$(LINK) $+ $(LINKFLAGS) -o $@


# copy files to install directory
INST_DIR := diskperf-0
install:
	$(MKDIR) $(INST_DIR)
	$(CP) \
		$(BIN) \
		$(DSPF_DIR)/README.md \
		$(INST_DIR)
	chmod 0644 $(INST_DIR)/*
	chmod 0755 $(INST_DIR)/$(BIN)


# package
PKG_VER := 0.1
PKG_ARCH := amd64
PKG_PACKER := tar -c --owner=0 --group=0 --numeric-owner -v --zstd -f
PKG_EXT := tar.zst
ifeq "$(OS)" "windows"
	PKG_ARCH := x64
	PKG_PACKER := zip -r -v
	PKG_EXT := zip
endif
package:
	$(PKG_PACKER) diskperf-$(PKG_VER)-$(OS)-$(PKG_ARCH).$(PKG_EXT) $(INST_DIR)
