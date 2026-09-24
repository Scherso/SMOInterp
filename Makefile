PWD := $(shell pwd)
MISC_PATH := $(PWD)/misc
MK_PATH := $(MISC_PATH)/mk
SCRIPTS_PATH := $(MISC_PATH)/scripts
SPECS_PATH := $(MISC_PATH)/specs

include $(PWD)/config.mk

NAME := $(shell basename $(PWD))
OUT := $(PWD)/deploy

LOAD_KIND := Module
LOAD_KIND_ENUM := 2
BINARY_NAME := subsdk9
SPECS_NAME := module.specs

.PHONY: clean all

EXL_CFLAGS   := $(C_FLAGS) -DEXL_LOAD_KIND=$(LOAD_KIND) -DEXL_LOAD_KIND_ENUM=$(LOAD_KIND_ENUM) -DEXL_PROGRAM_ID=0x$(PROGRAM_ID)
EXL_CXXFLAGS := $(CXX_FLAGS)

export

include $(MK_PATH)/common.mk
