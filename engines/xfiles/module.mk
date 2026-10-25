MODULE := engines/xfiles

MODULE_OBJS = \
	captions.o \
	computers.o \
	console.o \
	cursors.o \
	dialogbox.o \
	discfiles.o \
	headcredits.o \
	hot.o \
	interface.o \
	itemtext.o \
	menu.o \
	metaengine.o \
	movie.o \
	neoaccess.o \
	phone.o \
	pda.o \
	pictures.o \
	prefs.o \
	refrun.o \
	saveload.o \
	script.o \
	script_dialog.o \
	script_hooks.o \
	script_media.o \
	script_save.o \
	script_view.o \
	sound.o \
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
