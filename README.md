## Quake Lenses

This is a fork of [TyrQuake](http://disenchant.net/engine.html) integrated with [Fisheye Quake](http://strlen.com/gfxengine/fisheyequake/). Its purpose is to explore different ways to squeeze panoramic views on a small screen using different wide-angle lenses (map projections).

#### How is it different from Fisheye Quake?
* seamless cube maps
* camera rolling
* viewsize adjustment with status bar
* variety of fisheye lenses

#### Commands
    lens <0|1|2|3|4|5>
    hfov <horizontal degrees>
    vfov <vertical degrees>
    dfov <diagonal degrees>

#### Lens Descriptions:

0. Standard Perspective
1. Simple Fisheye
2. Mirror Ball
3. Low distortion Fisheye
4. Hemisphere
5. Stretched Rectangle

#### Actual Lens Names
0. Gnomonic, Perspective, Rectilinear
1. Azimuthal Equidistant (original fisheye)
2. Azimuthal Equisolid, Azimuthal Equal Area
3. Stereographic (another fisheye with less distortion)
4. Azimuthal Orthogonal, Sine-law
5. Equirectangular, Equidistant Cylindrical
