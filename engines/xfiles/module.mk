MODULE := engines/xfiles

MODULE_OBJS = \
	captions.o \
	console.o \
	dialogbox.o \
	discfiles.o \
	metaengine.o \
	movie.o \
	neoaccess.o \
	pictures.o \
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
