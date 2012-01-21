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


How does it work?
-----------------

All sorts of wide-angle perspectives can be constructed using variety of **globes and lenses**.

**Globes**

In the original Fisheye Quake mod, six 90x90 degree images are rendered at once to capture the full environment around a player.  Together, these images form a cubemap.  In blinky, we call that cube a type of **globe**.  A globe, or formally an environment map, is a custom way to capture your environment.

To define a **globe**, you create a Lua script in the globes/ folder, and define a *plates* list.  Think of each plate as a covering a certain area on the surface of your globe.  Each plate contains a forward vector, an up vector, and a field of view (FOV).  The game will configure the camera relative to the player using those parameters, and snap a picture.  After doing this for each plate, we have a full environment map.  Thus you can define your own way to capture your environment by defining these plates.

Blinky requires a way to retrieve a pixel from the **globe** given a view vector emanating from the player.  A default function is provided that just chooses the plate with the forward vector having the smallest angular distance from the given view vector, then does some math to retrieve the pixel from that plate.  Since some plates may overlap, you can provide your own in the globe script by defining a "function globe_plate(x,y,z)" that returns the index of the plate that you want the game to use for the given view vector.

**Lenses**

In the original Fisheye Quake mod, the pixels were taken from the cubemap and mapped to the screen using an Equidistant Fisheye projection.  In blinky, we call that a type of *lens*.  A lens, or formally *projection*, is a custom way to warp your environment to your screen.

To define a **lens**, you create a Lua script in the lenses/ folder.  There is a lot more to a lens than a globe which will take some explanation.

First, consider the *projection* to be the final image created by your lens script, which may be of any size depending on the equations you use.  Keep that in mind.  

A **forward** function is one that maps a view vector to a point on your projection.  An **inverse** function does the opposite by mapping a point on your projection to a view vector.  

Zooming in and out of your view in the game is equivalent to increasing and decreasing the scale at which this projection is drawn on the screen.  The projection is scaled by using either an **FOV command** (`hfov`, `vfov`), or a **fit command** (`fit`, `hfit`, `vfit`).  An FOV command will zoom in using conventional degrees that will span your screen horizontally or vertically.  A fit command will try to fit the entirety of the projection to the width or height of your screen, if the projection is not infinitely large, which is possibly the case sometimes.

For your lens script to support an **FOV command**, you must provide a `forward(x,y,z)` function and a `max_hfov` and `max_vfov` value to constrain the `hfov` and `vfov` degree values.  You can still provide the `inverse(x,y)` function (which computes faster), but `forward(x,y,z)` is still used to scale your projection to the screen.  It does this by inputting the view vector on the right side or top side of the screen (depending on if you're using `hfov` or `vfov`) to the `forward(x,y,z)` function in order to get the 2D coordinate in your projection.  Using this, it knows how to scale the 2D projection coordinates used in your script to fit the screen.

For the lens script to support a **fit command**, you can provide either a `forward(x,y,z)` or `inverse(x,y,z)` function.  You must also provide an `hfit_size` or `vfit_size` so that projection can be scaled to match those sizes to fit the screen.

The game will always look for an `inverse` function first, because it is faster to render.  The `forward` function only sparsely populates the pixels on the screen, so some interpolation is done to fill in the holes, which takes a bit longer.  This only matters for the time it takes to build the lookup table though, once it's done, the performance only relies on how many views has to be rendered by the globe.  The globe will actually not render any views that don't appear in the projection to prevent performance hits.

Still, it's possibly to increase the performance of the initial lookup table built from your lens. You can do this by exploiting the symmetry of your projection to lessen the number of calculations performed.  You can set `hsym` or `vsym` to true to indicate horizontal or vertical symmetry.  Horizontal symmetry means the left and right sides are reflections of each other.

Don't Panic
-----------

If you have no idea what's going on, that's because this is a weird project.  It just explores the simple question of what it might be like if you had eyes in the back of your head, I guess.  To get a non-nauseous perspective, I would just try the following:

    lens panini
    hfov 180
    
or

    lens stereographic
    hfov 180
    
I find it to be a perspective that best represents your real life range of view on a limiting screen.