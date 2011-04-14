# Quake Lenses

This is a fork of [TyrQuake](http://disenchant.net/engine.html) integrated with [Fisheye Quake](http://strlen.com/gfxengine/fisheyequake/). Its purpose is to explore different wide-angle views using well-known map projections.

#### How is it different from Fisheye Quake?
* seamless cube maps
* camera rolling
* viewsize adjustment with status bar
* variety of wide-angle lenses
* unfolded cubemap display

#### Commands
    lenses : shows help
    lens <0-11>
    nextlens
    prevlens
    hfov <horizontal degrees>
    vfov <vertical degrees>
    dfov <diagonal degrees>
    cube <0|1> : shows unfolded cubemap
    cube_rows <#> : number of rows in cubemap table display
    cube_cols <#> : number of columns in cubemap table display
    cube_order "########" : the faces to display in the table, in row order, use 9 for empty
    colorcube : paint the cubemap

#### Lens Projections:
* 0: Azimuthal Gnomonic: (all games use this)
* 1: Azimuthal Equidistant: (original fisheye)
* 2: Azimuthal Equal-Area: (mirror ball)
* 3: Azimuthal Equal-Area on Ellipse: (Hammer map)
* 4: Azimuthal Stereographic:
* 5: Azimuthal Orthogonal:
* 6: Water:
* 7: Cylindrical Gnomonic: (panorama)
* 8: Cylindrical Equidistant: (equirectangular map)
* 9: Cylindrical Conformal: (Mercator map)
* 10: Cylindrical Conformal Shrink: (Miller map)
* 11: Cylindrical Stereographic: (Braun map aka Panini)

#### Motion Sickness:
Some lenses may induce nausea! I find that the **stereographic** lenses do not, as they are the only true perspective projections here.  I recommend an fov of 180-200 using the stereographic lenses (4 and 11).
