PWD := $(shell pwd)
MISC_PATH := $(PWD)/misc
MK_PATH := $(MISC_PATH)/mk
SPECS_PATH := $(MISC_PATH)/specs

include $(PWD)/config.mk

NAME := $(shell basename $(PWD))
OUT := $(PWD)/deploy

LOAD_KIND := Module
LOAD_KIND_ENUM := 2
BINARY_NAME := subsdk9
SPECS_NAME := module.specs

MOD_NAME := SMOInterp
VERSION := $(shell git describe --tags --always 2>/dev/null || echo dev)
ZIP := $(PWD)/$(MOD_NAME)-$(VERSION).zip

.PHONY: clean all package

EXL_CFLAGS   := $(C_FLAGS) -DEXL_LOAD_KIND=$(LOAD_KIND) -DEXL_LOAD_KIND_ENUM=$(LOAD_KIND_ENUM) -DEXL_PROGRAM_ID=0x$(PROGRAM_ID)
EXL_CXXFLAGS := $(CXX_FLAGS)

export

ifeq ($(strip $(DEVKITPRO)),)
# No devkitPro toolchain on this machine: rerun make inside the official devkitA64 container.
# Mounted at /src, so exlaunch names its intermediate outputs src.*.
CONTAINER := $(shell command -v podman || command -v docker)
# Pinned so a toolchain update can't break the -Werror build unannounced. CI uses this too.
IMAGE := docker.io/devkitpro/devkita64:20260219

all clean:
	@[ -n "$(CONTAINER)" ] || { echo "need podman or docker, or set DEVKITPRO" >&2; exit 1; }
	@$(CONTAINER) run --rm -v "$(PWD)":/src:Z -w /src $(IMAGE) make -j$$(nproc) $@
else
include $(MK_PATH)/common.mk
endif

# Zip deploy/ in the emulator load-directory layout: <title id>/SMOInterp/exefs/{subsdk9,main.npdm}.
package: all
	@rm -f $(ZIP)
	@stage=$$(mktemp -d) && trap 'rm -rf "$$stage"' EXIT && \
		mkdir -p "$$stage/$(PROGRAM_ID)/$(MOD_NAME)/exefs" && \
		cp $(OUT)/$(BINARY_NAME) $(OUT)/main.npdm "$$stage/$(PROGRAM_ID)/$(MOD_NAME)/exefs/" && \
		cd "$$stage" && zip -qr $(ZIP) $(PROGRAM_ID)
	@echo $(ZIP)
