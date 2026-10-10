MODULE := engines/xfiles

MODULE_OBJS = \
	console.o \
	discfiles.o \
	metaengine.o \
	neoaccess.o \
	script.o \
	vcdata.o \
	vcdump.o \
	xfiles.o

# This module can be built as a plugin
ifeq ($(ENABLE_XFILES), DYNAMIC_PLUGIN)
PLUGIN := 1
endif

# Include common rules
include $(srcdir)/rules.mk

# Detection objects
DETECT_OBJS += $(MODULE)/detection.o
