# Quake Lenses

This is a fork of [TyrQuake](http://disenchant.net/engine.html) integrated with [Fisheye Quake](http://strlen.com/gfxengine/fisheyequake/). Its purpose is to explore different wide-angle views using well-known map projections.

#### How is it different from Fisheye Quake?
* seamless cube maps
* camera rolling
* viewsize adjustment with status bar
* variety of wide-angle lenses
* custom lens support with Lua scripts
* unfolded cubemap display

#### Custom Lenses
You can make your own lens by creating a Lua script file in the /lenses folder of your quake directory.  Your lens must map screen coordinates to orientation coordinates, or vice versa.  

Screen Coordinates:
* x,y: (0,0) at the center, +x right, +y down
* r: distance from the center of the screen

Orientation Coordinates:
* x,y,z: (0,0,1) straight ahead, +x right, +y up, +z back
* lat,lon: latitude, longitude
* theta: angle from center

#### Commands
    lenses : shows help
    lens <name> : choose a lens (<name>.lua in /lenses)
    hfov <horizontal degrees>
    vfov <vertical degrees>
    dfov <diagonal degrees>
    cube <0|1> : shows unfolded cubemap
    cube_rows <#> : number of rows in cubemap table display
    cube_cols <#> : number of columns in cubemap table display
    cube_order "########" : the faces to display in the table, in row order, use 9 for empty
    colorcube : paint the cubemap

#### Motion Sickness:
Some lenses may induce nausea! I find that the **stereographic** lenses do not, as they are the only true perspective projections here.  I recommend an fov of 180-200 using Stereographic, Panini, or Gumby
