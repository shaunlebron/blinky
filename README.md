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
    lens <#> : choose a lens (listed below)
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
* 0: Rectilinear: (all games use this)
* 1: Equidistant Fisheye: (original fisheye)
* 2: Equisolid-Angle Fisheye: (mirror ball)
* 3: Hammer:
* 4: Stereographic:
* 5: Orthogonal:
* 6: Cylinder: 
* 7: Equirectangular:
* 8: Mercator:
* 9: Miller:
* 10: Panini:

#### Motion Sickness:
Some lenses may induce nausea! I find that the **stereographic** lenses do not, as they are the only true perspective projections here.  I recommend an fov of 180-200 using Stereographic or Panini
