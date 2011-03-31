## Quake Lenses

### Based on
A fork of [TyrQuake](http://disenchant.net/engine.html) integrated with [Fisheye Quake](http://strlen.com/gfxengine/fisheyequake/)

### Purpose

To explore different ways to squeeze panoramic views on a small screen using different wide-angle lenses (map projections).

### How is it different from Fisheye Quake?

Fixes:

* seamless cube maps
* camera rolling
* viewsize adjustment with status bar

Improvements:

* variety of fisheye lenses
* horizontal, vertical, and diagonal FOV control (changing one will adjust the others)

### How do I use it?
The following commands are available:

horizontal FOV:
    hfov <degrees>

vertical FOV:
    vfov <degrees>

diagonal FOV:
    dfov <degrees>

lens type (gnomonic=0, azimuthal equidistant=1, cylindrical=2, stereographic=3):
    lens <0|1|2|3>

