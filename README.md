# Quake Lenses

This is a fork of [TyrQuake](http://disenchant.net/engine.html) integrated with [Fisheye Quake](http://strlen.com/gfxengine/fisheyequake/). Its purpose is to explore different wide-angle views using well-known map projections.

#### How is it different from Fisheye Quake?
* seamless cube map
* camera rolling
* viewsize adjustment with status bar
* variety of wide-angle lenses
* custom lens support with Lua scripts

#### Lenses
All lenses are stored in the /lenses folder of your quake directory.  You can study them and create your own.

#### Commands
    lenses : shows help
    lens <name> : choose a lens (<name>.lua in /lenses)
    hfov <horizontal degrees>
    vfov <vertical degrees>
    hfit : fit the lens horizontally
    vfit : fit the lens vertically
    fit : fit the whole lens
    colorcube : paint the cubemap

#### Motion Sickness:
Some lenses may induce nausea! I find that the **stereographic** lenses do not, as they are the only true perspective projections here.  I recommend an fov of 180-200 using Stereographic, Panini, or Gumby
