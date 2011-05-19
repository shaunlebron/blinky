#
# TyrQuake Makefile (tested under Linux and MinGW/Msys)
#
# By default, all executables will be built. If you want to just build one,
# just type e.g. "make tyr-quake". If the build dirs haven't been created yet,
# you might need to type "make prepare" first.
# 

TYR_VERSION_MAJOR = 0
TYR_VERSION_MINOR = 61
TYR_VERSION_BUILD =

TYR_VERSION = $(TYR_VERSION_MAJOR).$(TYR_VERSION_MINOR)$(TYR_VERSION_BUILD)

# ============================================================================
# User configurable options here:
# ============================================================================

BUILD_DIR        ?= build

DEBUG            ?= N# Compile with debug info
OPTIMIZED_CFLAGS ?= Y# Enable compiler optimisations (if DEBUG != Y)
USE_X86_ASM      ?= $(I386_GUESS)
X11BASE          ?= $(X11BASE_GUESS)
QBASEDIR         ?= .# Default basedir for quake data files (Linux/BSD only)
TARGET_OS        ?= $(HOST_OS)

# ============================================================================

.PHONY:	default clean

# ============================================================================

SYSNAME := $(shell uname -s)

ifneq (,$(findstring MINGW32,$(SYSNAME)))
HOST_OS = WIN32
TOPDIR := $(shell pwd -W)
else
ifneq (,$(findstring $(SYSNAME),FreeBSD NetBSD OpenBSD))
HOST_OS = UNIX
UNIX = bsd
TOPDIR := $(shell pwd)
else
ifneq (,$(findstring $(SYSNAME),Linux))
HOST_OS = UNIX
UNIX = linux
#UNIX = null
TOPDIR := $(shell pwd)
else
$(error OS type not detected.)
endif
endif
endif

ifeq ($(TARGET_OS),WIN32)
EXT = .exe
ifneq ($(HOST_OS),WIN32)
TARGET ?= $(MINGW_CROSS_GUESS)
CC = $(TARGET)-gcc
STRIP = $(TARGET)-strip
WINDRES = $(TARGET)-windres
endif
else
EXT =
endif

# ============================================================================
# Helper functions
# ============================================================================

# ----------------------------------------------
# Try to guess the location of X11 includes/libs
# ----------------------------------------------

X11DIRS = /usr/X11R7 /usr/local/X11R7 /usr/X11R6 /usr/local/X11R6
X11BASE_GUESS := $(shell \
	if [ -e /usr/include/X11/Xlib.h ] && \
		[ -e /usr/lib/libX11.a ]; then exit 0; fi; \
	if [ -e /usr/local/include/X11/Xlib.h ] && \
		[ -e /usr/local/lib/libX11.a ]; then exit 0; fi; \
	for DIR in $(X11DIRS); do \
            if [ -e $$DIR/include/X11/Xlib.h ] && \
		[ -e $$DIR/lib/libX11.a ]; then echo $$DIR; break; fi; \
	done )

# --------------------------------------------------------------------
# Try to guess the MinGW cross compiler
# - I think i386-... is the standard naming, but Debian uses i586-...
# --------------------------------------------------------------------

MINGW_CROSS_GUESS := $(shell \
	if which i586-mingw32msvc-gcc > /dev/null 2>&1; then \
		echo i586-mingw32msvc; \
	else \
		echo i386-mingw32msvc; \
	fi)

# --------------------------------
# GCC version and option checking
# --------------------------------

cc-version = $(shell sh $(TOPDIR)/scripts/gcc-version \
              $(if $(1), $(1), $(CC)))

cc-option = $(shell if $(CC) $(CFLAGS) $(1) -S -o /dev/null -xc /dev/null \
             > /dev/null 2>&1; then echo "$(1)"; else echo "$(2)"; fi ;)

cc-i386 = $(if $(subst __i386,,$(shell echo __i386 | $(CC) -E -xc - | tail -n 1)),Y,N)

GCC_VERSION := $(call cc-version)
I386_GUESS  := $(call cc-i386)

# ---------------------
# Special include dirs
# ---------------------
DX_INC    = $(TOPDIR)/dxsdk/sdk/inc
ST_INC    = $(TOPDIR)/scitech/include

# --------------
# Library stuff
# --------------
NQ_ST_LIBDIR = scitech/lib/win32/vc
QW_ST_LIBDIR = scitech/lib/win32/vc

NQ_W32_COMMON_LIBS = wsock32 winmm dxguid lua51
NQ_W32_SW_LIBS = mgllt ddraw
NQ_W32_GL_LIBS = opengl32 comctl32

NQ_UNIX_COMMON_LIBS = m X11 Xext Xxf86dga Xxf86vm lua dl
NQ_UNIX_GL_LIBS = GL

NQ_W32_SW_LFLAGS  := $(patsubst %,-l%,$(NQ_W32_SW_LIBS) $(NQ_W32_COMMON_LIBS))
NQ_W32_GL_LFLAGS  := $(patsubst %,-l%,$(NQ_W32_GL_LIBS) $(NQ_W32_COMMON_LIBS))
NQ_UNIX_SW_LFLAGS := $(patsubst %,-l%,$(NQ_UNIX_COMMON_LIBS))
NQ_UNIX_GL_LFLAGS := $(patsubst %,-l%,$(NQ_UNIX_COMMON_LIBS) $(NQ_UNIX_GL_LIBS))

ifneq ($(X11BASE),)
NQ_UNIX_SW_LFLAGS += -L$(X11BASE)/lib
NQ_UNIX_GL_LFLAGS += -L$(X11BASE)/lib
endif

# ---------------------------------------
# Define some build variables
# ---------------------------------------

STRIP   ?= strip
WINDRES ?= windres

CFLAGS := $(CFLAGS) -Wall -Wno-trigraphs

ifeq ($(DEBUG),Y)
CFLAGS += -g
else
ifeq ($(OPTIMIZED_CFLAGS),Y)
CFLAGS += -O2
# -funit-at-a-time is buggy for MinGW GCC > 3.2
# I'm assuming it's fixed for MinGW GCC >= 4.0 when that comes about
CFLAGS += $(shell if [ $(GCC_VERSION) -lt 0400 ] ;\
		then echo $(call cc-option,-fno-unit-at-a-time); fi ;)
CFLAGS += $(call cc-option,-fweb,)
CFLAGS += $(call cc-option,-frename-registers,)
CFLAGS += $(call cc-option,-ffast-math,)
endif
endif

# --------------------------------------------------------------------------
#  Each binary needs to build it's own object files in separate directories
#  due to the {NQ,QW}_HACK ifdefs still present in the common files.
# --------------------------------------------------------------------------

# (sw = software renderer, gl = OpenGL renderer, sv = server)
NQSWDIR	= $(BUILD_DIR)/nqsw
NQGLDIR	= $(BUILD_DIR)/nqgl
QWSWDIR	= $(BUILD_DIR)/qwsw
QWGLDIR	= $(BUILD_DIR)/qwgl
QWSVDIR	= $(BUILD_DIR)/qwsv

BUILD_DIRS = $(NQSWDIR) $(NQGLDIR) $(QWSWDIR) $(QWGLDIR) $(QWSVDIR)
APPS =	tyr-quake$(EXT) tyr-glquake$(EXT) \
	tyr-qwcl$(EXT) tyr-glqwcl$(EXT) \
	tyr-qwsv$(EXT)

default:	all

all:	prepare $(APPS)

.PHONY:	prepare
prepare:	$(BUILD_DIRS)

COMMON_CPPFLAGS := -DTYR_VERSION=$(TYR_VERSION) -DQBASEDIR="$(QBASEDIR)"
ifeq ($(DEBUG),Y)
COMMON_CPPFLAGS += -DDEBUG
else
COMMON_CPPFLAGS += -DNDEBUG
endif

ifeq ($(USE_X86_ASM),Y)
COMMON_CPPFLAGS += -DUSE_X86_ASM
endif

ifneq ($(X11BASE),)
COMMON_CPPFLAGS += -I$(X11BASE)/include
endif
COMMON_CPPFLAGS += -I$(TOPDIR)/include

NQSW_CPPFLAGS := $(COMMON_CPPFLAGS) -DNQ_HACK
NQGL_CPPFLAGS := $(COMMON_CPPFLAGS) -DNQ_HACK -DGLQUAKE
QWSW_CPPFLAGS := $(COMMON_CPPFLAGS) -DQW_HACK
QWGL_CPPFLAGS := $(COMMON_CPPFLAGS) -DQW_HACK -DGLQUAKE
QWSV_CPPFLAGS := $(COMMON_CPPFLAGS) -DQW_HACK -DSERVERONLY

NQSW_CPPFLAGS += -I$(TOPDIR)/NQ
NQGL_CPPFLAGS += -I$(TOPDIR)/NQ
QWSW_CPPFLAGS += -I$(TOPDIR)/QW/client
QWGL_CPPFLAGS += -I$(TOPDIR)/QW/client
QWSV_CPPFLAGS += -I$(TOPDIR)/QW/server -I$(TOPDIR)/QW/client

ifeq ($(TARGET_OS),WIN32)
NQSW_CPPFLAGS += -I/local/include -idirafter $(DX_INC) -idirafter $(ST_INC)
NQGL_CPPFLAGS += -idirafter $(DX_INC)
QWSW_CPPFLAGS += -idirafter $(DX_INC) -idirafter $(ST_INC)
QWGL_CPPFLAGS += -idirafter $(DX_INC)
endif

ifeq ($(TARGET_OS),UNIX)
NQSW_CPPFLAGS += -DELF -DX11
NQGL_CPPFLAGS += -DELF -DX11
QWSW_CPPFLAGS += -DELF -DX11
QWGL_CPPFLAGS += -DELF -DX11 -DGL_EXT_SHARED
QWSV_CPPFLAGS += -DELF
endif

$(BUILD_DIR)/nqsw/%.o:	CPPFLAGS = $(NQSW_CPPFLAGS)
$(BUILD_DIR)/nqgl/%.o:	CPPFLAGS = $(NQGL_CPPFLAGS)
$(BUILD_DIR)/qwsw/%.o:	CPPFLAGS = $(QWSW_CPPFLAGS)
$(BUILD_DIR)/qwgl/%.o:	CPPFLAGS = $(QWGL_CPPFLAGS)
$(BUILD_DIR)/qwsv/%.o:	CPPFLAGS = $(QWSV_CPPFLAGS)

# To make warnings more obvious, be less verbose as default
# Use 'make V=1' to see the full commands
ifdef V
  quiet =
else
  quiet = quiet_
endif

quiet_cmd_mkdir = '  MKDIR   $@'
      cmd_mkdir = mkdir -p $@

define do_mkdir
	@if [ ! -d $@ ]; then \
		echo $($(quiet)cmd_mkdir); \
		$(cmd_mkdir); \
	fi;
endef

# cmd_fixdep => Turn all pre-requisites into targets with no commands, to
# prevent errors when moving files around between builds (e.g. from NQ or QW
# dirs to the common dir.)
cmd_fixdep = \
	cp $(@D)/.$(@F).d $(@D)/.$(@F).d.tmp ; \
	sed -e 's/\#.*//' -e 's/^[^:]*: *//' -e 's/ *\\$$//' -e '/^$$/ d' \
	    -e 's/$$/ :/' < $(@D)/.$(@F).d.tmp >> $(@D)/.$(@F).d ; \
	rm -f $(@D)/.$(@F).d.tmp

cmd_cc_dep_c = \
	$(CC) -MM -MT $@ $(CPPFLAGS) -o $(@D)/.$(@F).d $< ; \
	$(cmd_fixdep)

quiet_cmd_cc_o_c = '  CC      $@'
      cmd_cc_o_c = $(CC) -c $(CPPFLAGS) $(CFLAGS) -o $@ $<

define do_cc_o_c
	@$(cmd_cc_dep_c);
	@echo $($(quiet)cmd_cc_o_c);
	@$(cmd_cc_o_c);
endef

cmd_cc_dep_rc = \
	$(CC) -x c-header -MM -MT $@ $(CPPFLAGS) -o $(@D)/.$(@F).d $< ; \
	$(cmd_fixdep)

quiet_cmd_windres_res_rc = '  WINDRES $@'
      cmd_windres_res_rc = $(WINDRES) -I $(<D) -i $< -O coff -o $@

define do_windres_res_rc
	@$(cmd_cc_dep_rc);
	@echo $($(quiet)cmd_windres_res_rc);
	@$(cmd_windres_res_rc);
endef

quiet_cmd_cc_link = '  LINK    $@'
      cmd_cc_link = $(CC) -o $@ $^ $(1)

define do_cc_link
	@echo $($(quiet)cmd_cc_link);
	@$(call cmd_cc_link,$(1))
endef

quiet_cmd_strip = '  STRIP   $(1)'
      cmd_strip = $(STRIP) $(1)

ifeq ($(DEBUG),Y)
do_strip=
else
define do_strip
	@echo $(call $(quiet)cmd_strip,$(1));
	@$(call cmd_strip,$(1));
endef
endif

DEPFILES = \
	$(wildcard $(BUILD_DIR)/nqsw/.*.d) \
	$(wildcard $(BUILD_DIR)/nqgl/.*.d) \
	$(wildcard $(BUILD_DIR)/qwsw/.*.d) \
	$(wildcard $(BUILD_DIR)/qwgl/.*.d) \
	$(wildcard $(BUILD_DIR)/qwsv/.*.d)

ifneq ($(DEPFILES),)
-include $(DEPFILES)
endif

$(NQSWDIR):	; $(do_mkdir)
$(NQGLDIR):	; $(do_mkdir)
$(QWSWDIR):	; $(do_mkdir)
$(QWGLDIR):	; $(do_mkdir)
$(QWSVDIR):	; $(do_mkdir)

$(BUILD_DIR)/nqsw/%.o:		common/%.S	; $(do_cc_o_c)
$(BUILD_DIR)/nqsw/%.o:		NQ/%.S		; $(do_cc_o_c)
$(BUILD_DIR)/nqsw/%.o:		common/%.c	; $(do_cc_o_c)
$(BUILD_DIR)/nqsw/%.o:		NQ/%.c		; $(do_cc_o_c)
$(BUILD_DIR)/nqsw/%.res:	common/%.rc	; $(do_windres_res_rc)
$(BUILD_DIR)/nqsw/%.res:	NQ/%.rc		; $(do_windres_res_rc)

$(BUILD_DIR)/nqgl/%.o:		common/%.S	; $(do_cc_o_c)
$(BUILD_DIR)/nqgl/%.o:		NQ/%.S		; $(do_cc_o_c)
$(BUILD_DIR)/nqgl/%.o:		common/%.c	; $(do_cc_o_c)
$(BUILD_DIR)/nqgl/%.o:		NQ/%.c		; $(do_cc_o_c)
$(BUILD_DIR)/nqgl/%.res:	common/%.rc	; $(do_windres_res_rc)
$(BUILD_DIR)/nqgl/%.res:	NQ/%.rc		; $(do_windres_res_rc)

$(BUILD_DIR)/qwsw/%.o:		common/%.S	; $(do_cc_o_c)
$(BUILD_DIR)/qwsw/%.o:		QW/client/%.S	; $(do_cc_o_c)
$(BUILD_DIR)/qwsw/%.o:		QW/common/%.S	; $(do_cc_o_c)
$(BUILD_DIR)/qwsw/%.o:		common/%.c	; $(do_cc_o_c)
$(BUILD_DIR)/qwsw/%.o:		QW/client/%.c	; $(do_cc_o_c)
$(BUILD_DIR)/qwsw/%.o:		QW/common/%.c	; $(do_cc_o_c)
$(BUILD_DIR)/qwsw/%.res:	common/%.rc	; $(do_windres_res_rc)
$(BUILD_DIR)/qwsw/%.res:	QW/client/%.rc	; $(do_windres_res_rc)

$(BUILD_DIR)/qwgl/%.o:		common/%.S	; $(do_cc_o_c)
$(BUILD_DIR)/qwgl/%.o:		QW/client/%.S	; $(do_cc_o_c)
$(BUILD_DIR)/qwgl/%.o:		QW/common/%.S	; $(do_cc_o_c)
$(BUILD_DIR)/qwgl/%.o:		common/%.c	; $(do_cc_o_c)
$(BUILD_DIR)/qwgl/%.o:		QW/client/%.c	; $(do_cc_o_c)
$(BUILD_DIR)/qwgl/%.o:		QW/common/%.c	; $(do_cc_o_c)
$(BUILD_DIR)/qwgl/%.res:	common/%.rc	; $(do_windres_res_rc)
$(BUILD_DIR)/qwgl/%.res:	QW/client/%.rc	; $(do_windres_res_rc)

$(BUILD_DIR)/qwsv/%.o:		QW/server/%.S	; $(do_cc_o_c)
$(BUILD_DIR)/qwsv/%.o:		QW/common/%.S	; $(do_cc_o_c)
$(BUILD_DIR)/qwsv/%.o:		common/%.S	; $(do_cc_o_c)
$(BUILD_DIR)/qwsv/%.o:		QW/server/%.c	; $(do_cc_o_c)
$(BUILD_DIR)/qwsv/%.o:		QW/common/%.c	; $(do_cc_o_c)
$(BUILD_DIR)/qwsv/%.o:		common/%.c	; $(do_cc_o_c)

# ----------------------------------------------------------------------------
# Normal Quake (NQ)
# ----------------------------------------------------------------------------

# Objects common to all versions of NQ, sources are c code
NQ_COMMON_C_OBJS = \
	cd_common.o	\
	chase.o		\
	cl_demo.o	\
	cl_input.o	\
	cl_main.o	\
	cl_parse.o	\
	cl_tent.o	\
	cmd.o		\
	common.o	\
	console.o	\
	crc.o		\
	cvar.o		\
	host.o		\
	host_cmd.o	\
	keys.o		\
   lens.o      \
	mathlib.o	\
	menu.o		\
	net_dgrm.o	\
	net_loop.o	\
	net_main.o	\
	pr_cmds.o	\
	pr_edict.o	\
	pr_exec.o	\
	r_part.o	\
	\
	rb_tree.o	\
	\
	sbar.o		\
	\
	shell.o		\
	\
	snd_dma.o	\
	snd_mem.o	\
	snd_mix.o	\
	sv_main.o	\
	sv_move.o	\
	sv_phys.o	\
	sv_user.o	\
	view.o		\
	wad.o		\
	world.o		\
	zone.o

NQ_COMMON_ASM_OBJS = \
	math.o		\
	snd_mixa.o	\
	worlda.o

# Used in both SW and GL versions of NQ on the Win32 platform
NQ_W32_C_OBJS = \
	cd_win.o	\
	conproc.o	\
	in_win.o	\
	net_win.o	\
	net_wins.o	\
	snd_win.o	\
	sys_win.o

NQ_W32_ASM_OBJS = \
	sys_wina.o

# Used in both SW and GL versions on NQ on the Unix platform
NQ_UNIX_C_OBJS = \
	cd_$(UNIX).o	\
	net_udp.o	\
	net_bsd.o	\
	snd_linux.o	\
	sys_linux.o	\
	x11_core.o	\
	in_x11.o

NQ_UNIX_ASM_OBJS = \
	sys_dosa.o

# Objects only used in software rendering versions of NQ
NQ_SW_C_OBJS = \
	d_edge.o	\
	d_fill.o	\
	d_init.o	\
	d_modech.o	\
	d_part.o	\
	d_polyse.o	\
	d_scan.o	\
	d_sky.o		\
	d_sprite.o	\
	d_surf.o	\
	d_vars.o	\
	d_zpoint.o	\
	draw.o		\
	model.o		\
	r_aclip.o	\
	r_alias.o	\
	r_bsp.o		\
	r_draw.o	\
	r_edge.o	\
	r_efrag.o	\
	r_light.o	\
	r_main.o	\
	r_misc.o	\
	r_sky.o		\
	r_sprite.o	\
	r_surf.o	\
	r_vars.o	\
	screen.o

NQ_SW_ASM_OBJS = \
	d_draw.o	\
	d_draw16.o	\
	d_parta.o	\
	d_polysa.o	\
	d_scana.o	\
	d_spr8.o	\
	d_varsa.o	\
	r_aclipa.o	\
	r_aliasa.o	\
	r_drawa.o	\
	r_edgea.o	\
	r_varsa.o	\
	surf16.o	\
	surf8.o

# Objects only used in software rendering versions of NQ on the Win32 Platform
NQ_W32_SW_C_OBJS = \
	vid_win.o

NQ_W32_SW_ASM_OBJS = \
	dosasm.o

# Objects only used in software rendering versions of NQ on the Unix Platform
NQ_UNIX_SW_C_OBJS = \
	vid_x.o

NQ_UNIX_SW_AMS_OBJS =

# Objects only used in OpenGL rendering versions of NQ
NQ_GL_C_OBJS = \
	drawhulls.o	\
	gl_draw.o	\
	gl_mesh.o	\
	gl_model.o	\
	gl_refrag.o	\
	gl_rlight.o	\
	gl_rmain.o	\
	gl_rmisc.o	\
	gl_rsurf.o	\
	gl_screen.o	\
	gl_warp.o

NQ_GL_ASM_OBJS =

# Objects only used in OpenGL rendering versions of NQ on the Win32 Platform
NQ_W32_GL_C_OBJS = \
	gl_vidnt.o

NQ_W32_GL_ASM_OBJS =

# Objects only used in OpenGL rendering versions of NQ on the Unix Platform
NQ_UNIX_GL_C_OBJS = \
	gl_vidlinuxglx.o

NQ_UNIX_GL_ASM_OBJS =

# Misc objects that don't seem to get used...
NQ_OTHER_ASM_OBJS = \
	d_copy.o	\
	sys_dosa.o

NQ_OTHER_C_OBJS = 

# =========================================================================== #

# Build the list of object files for each particular target
# (*sigh* - something has to be done about this makefile...)

NQ_W32_SW_OBJS := $(NQ_COMMON_C_OBJS)
NQ_W32_SW_OBJS += $(NQ_SW_C_OBJS)
NQ_W32_SW_OBJS += $(NQ_W32_C_OBJS)
NQ_W32_SW_OBJS += $(NQ_W32_SW_C_OBJS)
NQ_W32_SW_OBJS += winquake.res
ifeq ($(USE_X86_ASM),Y)
NQ_W32_SW_OBJS += $(NQ_COMMON_ASM_OBJS)
NQ_W32_SW_OBJS += $(NQ_SW_ASM_OBJS)
NQ_W32_SW_OBJS += $(NQ_W32_ASM_OBJS)
NQ_W32_SW_OBJS += $(NQ_W32_SW_ASM_OBJS)
else
NQ_W32_SW_OBJS += nonintel.o
endif

NQ_W32_GL_OBJS := $(NQ_COMMON_C_OBJS)
NQ_W32_GL_OBJS += $(NQ_GL_C_OBJS)
NQ_W32_GL_OBJS += $(NQ_W32_C_OBJS)
NQ_W32_GL_OBJS += $(NQ_W32_GL_C_OBJS)
NQ_W32_GL_OBJS += winquake.res
ifeq ($(USE_X86_ASM),Y)
NQ_W32_GL_OBJS += $(NQ_COMMON_ASM_OBJS)
NQ_W32_GL_OBJS += $(NQ_GL_ASM_OBJS)
NQ_W32_GL_OBJS += $(NQ_W32_ASM_OBJS)
NQ_W32_GL_OBJS += $(NQ_W32_GL_ASM_OBJS)
endif

NQ_UNIX_SW_OBJS := $(NQ_COMMON_C_OBJS)
NQ_UNIX_SW_OBJS += $(NQ_SW_C_OBJS)
NQ_UNIX_SW_OBJS += $(NQ_UNIX_C_OBJS)
NQ_UNIX_SW_OBJS += $(NQ_UNIX_SW_C_OBJS)
ifeq ($(USE_X86_ASM),Y)
NQ_UNIX_SW_OBJS += $(NQ_COMMON_ASM_OBJS)
NQ_UNIX_SW_OBJS += $(NQ_SW_ASM_OBJS)
NQ_UNIX_SW_OBJS += $(NQ_UNIX_ASM_OBJS)
NQ_UNIX_SW_OBJS += $(NQ_UNIX_SW_ASM_OBJS)
else
NQ_UNIX_SW_OBJS += nonintel.o
endif

NQ_UNIX_GL_OBJS := $(NQ_COMMON_C_OBJS)
NQ_UNIX_GL_OBJS += $(NQ_GL_C_OBJS)
NQ_UNIX_GL_OBJS += $(NQ_UNIX_C_OBJS)
NQ_UNIX_GL_OBJS += $(NQ_UNIX_GL_C_OBJS)
ifeq ($(USE_X86_ASM),Y)
NQ_UNIX_GL_OBJS += $(NQ_COMMON_ASM_OBJS)
NQ_UNIX_GL_OBJS += $(NQ_GL_ASM_OBJS)
NQ_UNIX_GL_OBJS += $(NQ_UNIX_ASM_OBJS)
NQ_UNIX_GL_OBJS += $(NQ_UNIX_GL_ASM_OBJS)
endif

# ------------------------
# Now, the build rules...
# ------------------------

# Win32
tyr-quake.exe:	$(patsubst %,$(NQSWDIR)/%,$(NQ_W32_SW_OBJS))
	$(call do_cc_link,-mwindows -L/local/lib -L$(NQ_ST_LIBDIR) $(NQ_W32_SW_LFLAGS))
	$(call do_strip,$@)

tyr-glquake.exe:	$(patsubst %,$(NQGLDIR)/%,$(NQ_W32_GL_OBJS))
	$(call do_cc_link,-mwindows -L$(NQ_ST_LIBDIR) $(NQ_W32_GL_LFLAGS))
	$(call do_strip,$@)

# Unix
tyr-quake:	$(patsubst %,$(NQSWDIR)/%,$(NQ_UNIX_SW_OBJS))
	$(call do_cc_link,$(NQ_UNIX_SW_LFLAGS))
	$(call do_strip,$@)

tyr-glquake:	$(patsubst %,$(NQGLDIR)/%,$(NQ_UNIX_GL_OBJS))
	$(call do_cc_link,$(NQ_UNIX_GL_LFLAGS))
	$(call do_strip,$@)


# ----------------------------------------------------------------------------
# QuakeWorld (QW) - Client
# ----------------------------------------------------------------------------

QW_SV_SHARED_C_OBJS = \
	cmd.o		\
	common.o	\
	crc.o		\
	cvar.o		\
	mathlib.o	\
	md4.o		\
	net_chan.o	\
	pmove.o		\
	pmovetst.o	\
	rb_tree.o	\
	shell.o		\
	zone.o

QW_COMMON_C_OBJS = \
	$(QW_SV_SHARED_C_OBJS) \
	cd_common.o	\
	cl_cam.o	\
	cl_demo.o	\
	cl_ents.o	\
	cl_input.o	\
	cl_main.o	\
	cl_parse.o	\
	cl_pred.o	\
	cl_tent.o	\
	console.o	\
	keys.o		\
	menu.o		\
	r_part.o	\
	sbar.o		\
	skin.o		\
	snd_dma.o	\
	snd_mem.o	\
	snd_mix.o	\
	view.o		\
	wad.o

QW_COMMON_ASM_OBJS = \
	math.o		\
	snd_mixa.o

QW_W32_C_OBJS = \
	cd_win.o	\
	in_win.o	\
	net_wins.o	\
	snd_win.o	\
	sys_win.o

QW_W32_ASM_OBJS = \
	sys_wina.o

QW_UNIX_SV_SHARED_C_OBJS = \
	net_udp.o

QW_UNIX_C_OBJS = \
	$(QW_UNIX_SV_SHARED_C_OBJS) \
	cd_$(UNIX).o	\
	snd_linux.o	\
	sys_linux.o	\
	in_x11.o	\
	x11_core.o

QW_UNIX_ASM_OBJS = \
	sys_dosa.o

QW_SW_C_OBJS = \
	d_edge.o	\
	d_fill.o	\
	d_init.o	\
	d_modech.o	\
	d_part.o	\
	d_polyse.o	\
	d_scan.o	\
	d_sky.o		\
	d_sprite.o	\
	d_surf.o	\
	d_vars.o	\
	d_zpoint.o	\
	draw.o		\
	model.o		\
	r_aclip.o	\
	r_alias.o	\
	r_bsp.o		\
	r_draw.o	\
	r_edge.o	\
	r_efrag.o	\
	r_light.o	\
	r_main.o	\
	r_misc.o	\
	r_sky.o		\
	r_sprite.o	\
	r_surf.o	\
	r_vars.o	\
	screen.o

QW_SW_ASM_OBJS = \
	d_draw.o	\
	d_draw16.o	\
	d_parta.o	\
	d_polysa.o	\
	d_scana.o	\
	d_spr8.o	\
	d_varsa.o	\
	r_aclipa.o	\
	r_aliasa.o	\
	r_drawa.o	\
	r_edgea.o	\
	r_varsa.o	\
	surf16.o	\
	surf8.o

QW_W32_SW_C_OBJS = \
	vid_win.o

QW_W32_SW_ASM_OBJS =

QW_UNIX_SW_C_OBJS = \
	vid_x.o

QW_UNIX_SW_ASM_OBJS =

QW_GL_C_OBJS = \
	drawhulls.o	\
	gl_draw.o	\
	gl_mesh.o	\
	gl_model.o	\
	gl_ngraph.o	\
	gl_refrag.o	\
	gl_rlight.o	\
	gl_rmain.o	\
	gl_rmisc.o	\
	gl_rsurf.o	\
	gl_screen.o	\
	gl_warp.o

QW_GL_ASM_OBJS =

QW_W32_GL_C_OBJS = \
	gl_vidnt.o

QW_W32_GL_ASM_OBJS =

QW_UNIX_GL_C_OBJS = \
	gl_vidlinuxglx.o

QW_UNIX_GL_ASM_OBJS =

# ========================================================================== #

# Build the list of object files for each particular target
# (*sigh* - something has to be done about this makefile...)

QW_W32_SW_OBJS := $(QW_COMMON_C_OBJS)
QW_W32_SW_OBJS += $(QW_SW_C_OBJS)
QW_W32_SW_OBJS += $(QW_W32_C_OBJS)
QW_W32_SW_OBJS += $(QW_W32_SW_C_OBJS)
QW_W32_SW_OBJS += winquake.res
ifeq ($(USE_X86_ASM),Y)
QW_W32_SW_OBJS += $(QW_COMMON_ASM_OBJS)
QW_W32_SW_OBJS += $(QW_SW_ASM_OBJS)
QW_W32_SW_OBJS += $(QW_W32_ASM_OBJS)
QW_W32_SW_OBJS += $(QW_W32_SW_ASM_OBJS)
else
QW_W32_SW_OBJS += nonintel.o
endif

QW_W32_GL_OBJS := $(QW_COMMON_C_OBJS)
QW_W32_GL_OBJS += $(QW_GL_C_OBJS)
QW_W32_GL_OBJS += $(QW_W32_C_OBJS)
QW_W32_GL_OBJS += $(QW_W32_GL_C_OBJS)
QW_W32_GL_OBJS += winquake.res
ifeq ($(USE_X86_ASM),Y)
QW_W32_GL_OBJS += $(QW_COMMON_ASM_OBJS)
QW_W32_GL_OBJS += $(QW_GL_ASM_OBJS)
QW_W32_GL_OBJS += $(QW_W32_ASM_OBJS)
QW_W32_GL_OBJS += $(QW_W32_GL_ASM_OBJS)
endif

QW_UNIX_SW_OBJS := $(QW_COMMON_C_OBJS)
QW_UNIX_SW_OBJS += $(QW_SW_C_OBJS)
QW_UNIX_SW_OBJS += $(QW_UNIX_C_OBJS)
QW_UNIX_SW_OBJS += $(QW_UNIX_SW_C_OBJS)
ifeq ($(USE_X86_ASM),Y)
QW_UNIX_SW_OBJS += $(QW_COMMON_ASM_OBJS)
QW_UNIX_SW_OBJS += $(QW_SW_ASM_OBJS)
QW_UNIX_SW_OBJS += $(QW_UNIX_ASM_OBJS)
QW_UNIX_SW_OBJS += $(QW_UNIX_SW_ASM_OBJS)
else
QW_UNIX_SW_OBJS += nonintel.o
endif

QW_UNIX_GL_OBJS := $(QW_COMMON_C_OBJS)
QW_UNIX_GL_OBJS += $(QW_GL_C_OBJS)
QW_UNIX_GL_OBJS += $(QW_UNIX_C_OBJS)
QW_UNIX_GL_OBJS += $(QW_UNIX_GL_C_OBJS)
ifeq ($(USE_X86_ASM),Y)
QW_UNIX_GL_OBJS += $(QW_COMMON_ASM_OBJS)
QW_UNIX_GL_OBJS += $(QW_GL_ASM_OBJS)
QW_UNIX_GL_OBJS += $(QW_UNIX_ASM_OBJS)
QW_UNIX_GL_OBJS += $(QW_UNIX_GL_ASM_OBJS)
endif

# ---------
# QW Libs
# ---------
QW_W32_COMMON_LIBS = wsock32 dxguid winmm
QW_W32_SW_LIBS = mgllt
QW_W32_GL_LIBS = opengl32 comctl32

QW_UNIX_COMMON_LIBS = m X11 Xext Xxf86dga Xxf86vm
QW_UNIX_GL_LIBS = GL

QW_W32_SW_LFLAGS  := $(patsubst %,-l%,$(QW_W32_SW_LIBS) $(QW_W32_COMMON_LIBS))
QW_W32_GL_LFLAGS  := $(patsubst %,-l%,$(QW_W32_GL_LIBS) $(QW_W32_COMMON_LIBS))
QW_UNIX_SW_LFLAGS := $(patsubst %,-l%,$(QW_UNIX_COMMON_LIBS))
QW_UNIX_GL_LFLAGS := $(patsubst %,-l%,$(QW_UNIX_COMMON_LIBS) $(QW_UNIX_GL_LIBS))

ifneq ($(X11BASE),)
QW_UNIX_SW_LFLAGS += -L$(X11BASE)/lib
QW_UNIX_GL_LFLAGS += -L$(X11BASE)/lib
endif

# ---------------------
# build rules
# --------------------

# Win32
tyr-qwcl.exe:	$(patsubst %,$(QWSWDIR)/%,$(QW_W32_SW_OBJS))
	$(call do_cc_link,-mwindows -L$(QW_ST_LIBDIR) $(QW_W32_SW_LFLAGS))
	$(call do_strip,$@)

tyr-glqwcl.exe:	$(patsubst %,$(QWGLDIR)/%,$(QW_W32_GL_OBJS))
	$(call do_cc_link,-mwindows $(QW_W32_GL_LFLAGS))
	$(call do_strip,$@)

# Unix
tyr-qwcl:	$(patsubst %,$(QWSWDIR)/%,$(QW_UNIX_SW_OBJS))
	$(call do_cc_link,$(QW_UNIX_SW_LFLAGS))
	$(call do_strip,$@)

tyr-glqwcl:	$(patsubst %,$(QWGLDIR)/%,$(QW_UNIX_GL_OBJS))
	$(call do_cc_link,$(QW_UNIX_GL_LFLAGS))
	$(call do_strip,$@)

UNUSED_OBJS	= cd_audio.o

# --------------------------------------------------------------------------
# QuakeWorld (QW) - Server
# --------------------------------------------------------------------------

QWSV_SHARED_OBJS = \
	cmd.o		\
	common.o	\
	crc.o		\
	cvar.o		\
	mathlib.o	\
	md4.o		\
	net_chan.o	\
	pmove.o		\
	pmovetst.o	\
	rb_tree.o	\
	shell.o		\
	zone.o

QWSV_W32_SHARED_OBJS = \
	net_wins.o

QWSV_UNIX_SHARED_OBJS = \
	net_udp.o

QWSV_ONLY_OBJS = \
	model.o		\
	pr_cmds.o	\
	pr_edict.o	\
	pr_exec.o	\
	sv_ccmds.o	\
	sv_ents.o	\
	sv_init.o	\
	sv_main.o	\
	sv_move.o	\
	sv_nchan.o	\
	sv_phys.o	\
	sv_send.o	\
	sv_user.o	\
	world.o

QWSV_W32_ONLY_OBJS = \
	sys_win.o

QWSV_UNIX_ONLY_OBJS = \
	sys_unix.o

QWSV_W32_OBJS = \
	$(QWSV_SHARED_OBJS) 	\
	$(QWSV_W32_SHARED_OBJS)	\
	$(QWSV_ONLY_OBJS)	\
	$(QWSV_W32_ONLY_OBJS)

QWSV_UNIX_OBJS = \
	$(QWSV_SHARED_OBJS) 		\
	$(QWSV_UNIX_SHARED_OBJS)	\
	$(QWSV_ONLY_OBJS)		\
	$(QWSV_UNIX_ONLY_OBJS)

# ----------------
# QWSV Libs
# ----------------
QWSV_W32_LIBS = wsock32 winmm
QWSV_W32_LFLAGS = -mconsole $(patsubst %,-l%,$(QWSV_W32_LIBS))
QWSV_UNIX_LIBS = m
QWSV_UNIX_LFLAGS = $(patsubst %,-l%,$(QWSV_UNIX_LIBS))

# -------------
# Build rules
# -------------

# Win32
tyr-qwsv.exe:	$(patsubst %,$(QWSVDIR)/%,$(QWSV_W32_OBJS))
	$(call do_cc_link,$(QWSV_W32_LFLAGS))
	$(call do_strip,$@)

# Unix
tyr-qwsv:	$(patsubst %,$(QWSVDIR)/%,$(QWSV_UNIX_OBJS))
	$(call do_cc_link,$(QWSV_UNIX_LFLAGS))
	$(call do_strip,$@)

# ----------------------------------------------------------------------------
# Very basic clean target (can't use xargs on MSYS)
# ----------------------------------------------------------------------------

# Main clean function...
clean:
	@rm -rf build
	@rm -f $(shell find . \( \
		-name '*~' -o -name '#*#' -o -name '*.o' -o -name '*.res' \
	\) -print)
