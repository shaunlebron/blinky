# Fisheye upgrade for TyrQuake

I'm preparing [blinky](http://github.com/shaunlebron/blinky) to be an official
patch to [TyrQuake](http://disenchant.net/tyrquake/).  It is a fisheye mod for
Quake in software rendering mode.  Changes will include: better defaults,
non-intrusive edits that can be turned off, non-blocking lens computing, etc.

![mercator_anim](mercator_anim.gif)

## Setup

1. Install [Lua](http://www.lua.org/)
1. `make bin/tyr-quake`
1. Copy lenses/ and globes/ to the tyrquake user directory "~/.tyrquake"
1. You should see Quake in 180º using the Panini projection.

## Commands

```
fisheye <0|1>     : enable/disable fisheye mode
f_globe <name>    : choose a globe (affects picture quality and render speed)
f_lens <name>     : choose a lens (affects the shape of your view)
f_fov <degrees>   : zoom to a horizontal FOV
f_vfov <degrees>  : zoom to a vertical FOV
f_cover           : zoom in until screen is covered (some parts may be hidden)
f_contain         : zoom out until screen contains the entire image (if possible)
f_rubix           : display colored grid for each rendered view in the globe
f_saveglobe       : take screenshots of each globe face (environment map)
```

## Patch

I'm generating the current [fisheye patch](fisheye.patch) with the command:

```
git diff 23119f4eb2ac6b5cef3e1ebfc785189b011aae26.. NQ common include Makefile
```

(The patch does not include the new files "NQ/lens.c" and "include/lens.h", so
you can better see the existing changes to the engine)
