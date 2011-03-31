## Quake Lenses

This is a fork of [TyrQuake](http://disenchant.net/engine.html) integrated with [Fisheye Quake](http://strlen.com/gfxengine/fisheyequake/). Its purpose is to explore different ways to squeeze panoramic views on a small screen using different wide-angle lenses (map projections).

#### How is it different from Fisheye Quake?
* seamless cube maps
* camera rolling
* viewsize adjustment with status bar
* variety of fisheye lenses
* horizontal, vertical, and diagonal FOV control (changing one will adjust the others)

#### Commands
    lens <0|1|2|3>
    hfov <horizontal degrees>
    vfov <vertical degrees>
    dfov <diagonal degrees>

Lens Types:

0. Gnomonic (standard)
1. Azimuthal Equidistant (original fisheye)
2. Equidistant Cylindrical
3. Stereographic (another fisheye with less distortion)
