blinky
======

Blinky is a fisheye mod for Quake forked from [Fisheye Quake](http://strlen.com/gfxengine/fisheyequake/). It was created to explore several types of panoramic projections defined by Lua scripts.

[View the official page for details and media](http://shaunew.github.com/blinky)

Video
-----

I made a video starting with the regular FOV stretched to its limits.  Then I tried a bunch of different projections in the rest of the video using Quake Done Quick demos: [View the video](http://www.youtube.com/watch?v=jQOJ3yCK8pI)

Navigating the Repository
-------------------------

The bulk of the code here belongs to [TyrQuake](http://disenchant.net/engine.html), which is the engine that **blinky** is integrated into.

Nearly all of the code relevant to blinky is in [NQ/lens.c](https://github.com/shaunew/blinky/blob/master/NQ/lens.c).  Other minor edits to files in the *NQ/* folder have been tagged with "FISHEYE" comments.  The Lua scripts that are used by *blinky* to edit your view are under [globes/](https://github.com/shaunew/blinky/tree/master/globes) and [lenses/](https://github.com/shaunew/blinky/tree/master/lenses).


In-Game Console Commands
------------------------

    globe <name>   : choose a globe (affects picture quality and render speed)
    lens <name>    : choose a lens (affects the shape of your view)
    hfov <degrees> : zoom by specifying horizontal FOV
    vfov <degrees> : zoom by specifying vertical FOV
    hfit           : zoom by fitting your view horizontal bounds in the screen
    hfit           : zoom by fitting your view's vertical bounds in the screen
    fit            : zoom by fitting your whole view in the screen
    rubix          : display colored grid for each rendered view in the globe

Compiling and Installation:
---------------------------

**Linux:**

1. Install LuaJIT
2. build tyr-quake:

        make prepare
        make tyr-quake

3. copy tyr-quake to your Quake directory
4. copy globes/ and lenses/ folder to $HOME/.tyrquake


**Windows:**

1. build tyr-quake.exe:

        make prepare
        make tyr-quake.exe

2. copy everything from the installation binary at the link above to your Quake directory, along with the freshly built tyr-quake.exe
