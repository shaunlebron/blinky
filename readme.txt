-----------
 Tyr-Quake
-----------

Date:		2009-07-05
Version: 	0.61
Author:		Kevin Shanahan (aka. Tyrann)
Webpage:	http://disenchant.net
email:		tyrann@disenchant.net

Why?
----
This is meant to be a fairly conservative branch of the Quake source
code. It's intended to support Quake and Quakeworld in both software and GL
versions, as well as the Quakeworld Server; all on both MS Windows and Linux
(BSD supported as best I can manage with some help from some other users). I
don't intend on changing the look or feel of the game by adding lots of
rendering enhancements, etc, but rather just fixing little bugs that I've come
across over the years. I'll be adding small enhancements and may even rewrite
substantial portions of the code, but I don't want to change the fundamental
feel of the game.

Building:
---------
All you should need to do to get a regular build for your system is
type 'make'. This should build all five executable targets. Windows
builds can be done using MinGW and Msys on a Windows host, or by cross
compiling with a suitable MinGW cross compiler. As long as your cross
compiler is in your path somewhere, "make TARGET_OS=WIN32" should be
all you need.

If you're in a hurry and only want to build one target, you can type 'make
prepare' (this sets up the build directories) followed by 'make <target>' -
where <target> is the name of the executable you want to build.

To build a debug version or one without the intel assembly, there are options
you can select by setting Makefile variables:
  e.g. make DEBUG=Y prepare tyr-glquake
    or make USE_X86_ASM=N prepare tyr-qwcl

Version History:

v0.61
=====
- Fix QWSV command line parsing
- Attempt to detect when X86 assembler files should be used

v0.60
=====
- Fix video buffer overrun when rendering endtitle in low-res vid mode
- Reduce the load timeout when a changelevel fails

v0.59
=====
- Various improvements to the windows video code
  - Start in windowed mode to avoid extra mode changes on the monitor
  - Fix logic for mouse grab and release when console or menu is active
- Fix handling of sound files with incorrect headers (fixes SoE crash)
- Increase software renderer's MAX_MOD_KNOWN (fixes Contract Revoked crash)
- Various other minor cleanups and code improvements

v0.58
=====
- Various net fixes and cleanups from O.Sezer
- Fixed mouse wheel support with MS "Direct Input" and made direct input the
  default (disable with -nodinput).
- Added some cross compiling support to the main makefile (MinGW32 target)
- Remove the MAX_PRSTR limit (was set too low anyway) 

v0.57
=====
- Various 64 bit correctness fixes. All executables now work at least on 64 bit
  Linux, as long as you build with USE_X86_ASM=N.
- Removed a few pieces of dead/legacy code. No more "-record" and "-playback"
  options (net_vcr) and no more IPX or Serial/Modem networking either.
- Fixed a fairly rare memory corruption issue due to poor handling of BSPs
  having more than one sky texture in glquake.
- Various other minor fixes and code cleanups.

v0.56
=====
- Added "-developer" command line argument (equivalent to "developer 1" at the
  console, but activates very early during startup)
- "-w" is equivalent to "-window" on the command line
- Fixed potential crash on startup when hostname is not set
- Various fixes for big-endian builds
- Now works on Linux/PPC!

v0.55
=====
- Fix a crash provoked by the qd100qlite2 mod
- Refactor the cdaudio system, adding a BSD driver
- Allow user to add custom data/config files in $HOME/.tyrquake
- Add console stretch effect ("gl_constretch 1" to enable)
- Makefile cleanups to aid customisations for packagers

v0.54
=====
- Remove some no longer required rendering code paths (gl_texsort 0,
  _gl_sky_mtex 0, _gl_lightmap_sort 1)
- Use API generated OpenGL texture handles, instead of our own. This is my
  first baby step before looking at some decent texture management.
- Share a few more files between NQ/QW
- Improved build dependencies to handle moved files

v0.53
=====
- Add command argument completion for changelevel
- Re-organised the build system, proper auto dependency generation
- Fixed sound issue when compiling with GCC 4.1 (compiler bug)
- Fix QW option menu, "use mouse" option now usable.
- Fix "particle's look like triangles" GL renderer bug

v0.52
=====
- move cmd.[ch] into common directories
- increase clipnode limit to 65520 (was 32767)

v0.51
=====
- Work around problems with MinGW upgrade
- Merge sv_move, r_alias and r_sprite into common
- STree api additions and cleanups
- Replace old completion framework completely with strees
- Other minor fixes and source formatting changes

v0.50
=====
- Added command argument completion infrastructure
- Added argument completion for map, playdemo and timedemo commands

v0.49
=====
- Better fix for glXGetProcAddress ABI issues on Linux
- Add "maplist" command - lists maps in the current path(s)
- Enable command completion after ';' on a line
- Fix problem with really long GL extension strings (e.g. NVidia/Linux)

v0.48
=====
- Save mlook state to config.cfg
- Make mousewheel work in Linux
- Make CD volume control work in Linux
- Make gamma controls work in Linux/Windows GLQuake
- Thanks to Stephen A for supplying the patches used as a basis for the above
  (and Ozkan for some of the original work)

v0.47
=====
- Add fullscreen modes to software quake in Linux
- Added r_drawflat to glquake, glqwcl
- Fixed r_waterwarp in glquake (though it still looks crap)
- Multitexture improvements (sky, also usable with gl_texsort 1)
- Add rendering of collision hulls (via cvar _gl_drawhull for now)

v0.46
=====
- Fixed default vidmodes in windows, software NQ/QW (broken in v0.0.39 I think)
- Fixed sound channel selection broken in v0.45
- Fixed scaling of non-default sized console backgrounds

v0.45
=====
- Changed to a simpler version numbering system (fits the console better too!)
- Makefile tweaks; can optionally build with no intel asm files now.
- Started moving around bits of the net code. No behaviour changes intended.
- Con_Printf only triggers screen updates with "\n" now.
- Various other aimless code cleanups (comments, preprocessor bits)

v0.0.44
=======
- Fix the previous SV_TouchLinks fix (oops!)
- Make AllocBlock more efficient for huge maps

v0.0.43
=======
- Fixed a rare crash in SV_TouchLinks

v0.0.42
=======
- Increased max verticies/triangles on mdls

v0.0.41
=======
- fixed marksurfaces overflow in bsp loading code (fixes visual corruption on
  some very large maps)

v0.0.40
=======
- added the high-res modes to the QW software renderer as well
- fixed a rendering bug when cl_bobcycle was set to zero

v0.0.39
=======
- Hacked in support for higher res windowed modes in software renderer. Only in
  NQ for now, add to QW later.
- gl_model.c now a shared file
- Random cleanups

v0.0.38
=======
- Fixed a corruption/crash bug in model.c/gl_model.c bsp loading code.

v0.0.37
=======
- Cleaned up the tab-completion code a bit

v0.0.36 (and earlier)
=======
- Re-indent code to my liking
- Make changes to compile using gcc/mingw/msys
- Fix hundreds of warnings spit out by the compiler
- Lots of work on eliminating duplication of code (much more to do too!)
- Tried to reduce the enormous number of exported variables/functions.
- Fixed some of the input handling under Linux...
- Fixed initialisation order of OSS sound driver
- Hacked a max texture size detection fix in (should be using proxy textures?)
- Replaced SGIS multitexturing with ARB multitexture
- Added cvars "r_lockpvs" and "r_lockfrustum"
- Enhanced the console tab completion
- Bumped the edict limit up to 2048; various other limits bumped also...
- lots of other trivial things I've probably completely forgotten in the many
  months I've been picking over the code trying to learn more about it
