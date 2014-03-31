# Hyper Wide FOV in Quake

This is a fisheye addon for [TyrQuake](http://disenchant.net/tyrquake/).  It is
an evolution of [Fisheye Quake](http://strlen.com/gfxengine/fisheyequake/) that
fully explores the potential of panoramic gaming.  With it, you can achieve practical,
hyper-wide FOVs never before seen in games.

![mercator_anim](mercator_anim.gif)

_View above with `f_lens mercator`, toggling `f_rubix`_

## How does it work?

View construction happens in two stages. 1) Capture surrounding pixels with
multiple camera shots.  2) Stitch the shots together on the screen to get a
hyper-wide FOV. These stages are done by Lua scripts; namely a Globe script and
Lens script, respectively.  Your view can be customized by swapping each script
out for another; choose from several presets or even create your own.

__NOTE__: See the beginning of `NQ/fisheye.c` for extensive documentation and
diagrams describing the fisheye process in detail.

## Setup

1. Install [Lua](http://www.lua.org/)
1. `make bin/tyr-quake`
1. Copy lenses/ and globes/ to the tyrquake user directory "~/.tyrquake"
1. You should see Quake in 180º using the Panini projection.

## Commands

```
fisheye <0|1>     : enable/disable fisheye mode
f_help            : show quick start options
f_globe <name>    : choose a globe (affects picture quality and render speed)
f_lens <name>     : choose a lens (affects the shape of your view)
f_fov <degrees>   : zoom to a horizontal FOV
f_vfov <degrees>  : zoom to a vertical FOV
f_cover           : zoom in until screen is covered (some parts may be hidden)
f_contain         : zoom out until screen contains the entire image (if possible)
f_rubix           : display colored grid for each rendered view in the globe
f_saveglobe       : take screenshots of each globe face (environment map)
```

## The Patch

[This patch](fisheye.patch) contains all changes to EXISTING files in TyrQuake.

These are the only NEW files:

- NQ/fisheye.c
- include/fisheye.h
- lenses/*.lua
- globes/*.lua

### Patch-generation

I generate the [fisheye patch](fisheye.patch) with the command:

```
git diff 23119f4eb2ac6b5cef3e1ebfc785189b011aae26.. NQ common include Makefile
```

