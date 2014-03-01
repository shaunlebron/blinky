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
fisheye <0|1>  : enable/disable fisheye mode
globe <name>   : choose a globe (affects picture quality and render speed)
lens <name>    : choose a lens (affects the shape of your view)
hfov <degrees> : zoom by specifying horizontal FOV
vfov <degrees> : zoom by specifying vertical FOV
hfit           : zoom by fitting your view horizontal bounds in the screen
hfit           : zoom by fitting your view's vertical bounds in the screen
fit            : zoom by fitting your whole view in the screen
rubix          : display colored grid for each rendered view in the globe
saveglobe      : take screenshots of each globe face (environment map)
```

## Patch

I'm generating the current [fisheye patch](fisheye.patch) with the command:

```
git diff 23119f4eb2ac6b5cef3e1ebfc785189b011aae26.. NQ common include Makefile
```

(The patch does not include the new files "NQ/lens.c" and "include/lens.h", so
you can better see the existing changes to the engine)
