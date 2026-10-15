MODULE := engines/xfiles

MODULE_OBJS = \
	captions.o \
	computers.o \
	console.o \
	cursors.o \
	dialogbox.o \
	discfiles.o \
	headcredits.o \
	interface.o \
	metaengine.o \
	movie.o \
	neoaccess.o \
	phone.o \
	pda.o \
	pictures.o \
	saveload.o \
	script.o \
	subliminal.o \
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
