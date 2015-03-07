# LENSES

A "lens" script takes the camera views provided by a "globe" script and melds
them into a single image.  It expects the following symbols, which we will
go into more detail:

__MAPPING FUNCTIONS__:

- `lens_forward` (function (x,y,z) -> (x,y))
- `lens_inverse` (function (x,y) -> (x,y,z))

__BOUNDARY CONSTANTS__:

- `lens_width` (double)
- `lens_height` (double)
- `max_fov` (int)
- `max_vfov` (int)

__LOAD COMMAND__:

- `onload` (string)


The following symbols are provided for your use:
   
- `latlon_to_ray` (function (lat,lon) -> (x,y,z))
- `ray_to_latlon` (function (x,y,z) -> (lat,lon))
- `plate_to_ray` (function (i,u,v) -> (x,y,z))
- `numplates` (int)

## Mapping

The lens creates a mapping between:

- a 2D coordinate on the projected image
- a 3D coordinate on the globe (direction vector)

The mapping can be specified either with a `lens_forward` or `lens_inverse`
function, allowing you to decide the mapping's direction.


```
            GLOBE plates                                  LENS projection

             ---------
             |       |
             |       |                              -----------------------------
             |       |                              |\--                     --/|
             ---------                      FORWARD |   \---             ---/   |
   --------- --------- --------- ---------  ------> |       \-----------/       |
   |       | |       | |       | |       |          |        |         |        |
   |       | |       | |       | |       |          |        |         |        |
   |       | |       | |       | |       |          |        |         |        |
   --------- --------- --------- ---------  <------ |       /-----------\       |
             ---------                      INVERSE |   /---             ---\   |
             |       |                              |/--                     --\|
             |       |                              -----------------------------
             |       |
             ---------
```

- A "FORWARD" mapping does GLOBE -> LENS.
- An "INVERSE" mapping does LENS -> GLOBE (this is faster!)

By default, the game will always prefer the inverse mapping if provided since
it is a lot faster.  With the forward mapping, there is a lot of extra
processing and interpolation done to produce a final image.

For example, [fisheye1.lua](fisheye1.lua) provides both mappings:

```lua
function lens_inverse(x,y)
   local r = sqrt(x*x+y*y)

   if r > pi then
      return nil
   end
   local theta = r

   local s = sin(theta)
   return x/r*s, y/r*s, cos(theta)
end

function lens_forward(x,y,z)
   local theta = acos(z)

   local r = theta

   local c = r/sqrt(x*x+y*y)
   return x*c, y*c
end
```

## Globe Coordinate Systems

The coordinate received by `lens_forward` and the coordinates outputted by
`lens_inverse` are expected to be direction vectors, but you can create
intermediate representations of them in different coordinate systems with the
following utility functions:
   
- `latlon_to_ray` (function (lat,lon) -> (x,y,z))
- `ray_to_latlon` (function (x,y,z) -> (lat,lon))
- `plate_to_ray` (function (i,u,v) -> (x,y,z))

Available coordinate systems:

- direction vector

    ```
         +Y = up
            ^
            |
            |
            |    / +Z = forward
            |   /
            |  /
            | /
            0------------------> +X = right
    ```

- latitude/longitude (spherical degrees)

    ```
         +latitude (degrees up)
            ^
            |
            |
            |
            |
            |
            |
            0------------------> +longitude (degrees right)
    ```

- plate index & uv (e.g. plate=1, u=0.5, v=0.5 to get the center pixel of plate 1)

    ```
            0----------> +u (max 1)
            | ---------
            | |       |
            | |       |
            | |       |
            | ---------
            V
            +v (max 1)
    ```


## Lens Coordinate System

The expected coordinate system of the projected image:

```
       +Y
        ^
        |
        |
        |
        |
        |
        |
        0----------------> +X
```

If the projected image has a finite width or height, you can define them here
to allow for fitting it to the screen (details in next section):

- `lens_width` (double)
- `lens_height` (double)


## Zooming

To control how much of the resulting lens image we can see on screen,
we scale it such that the screen aligns with certain points on the lens' axes.

### Using FOV

To use Field of View (FOV) to determine your zoom level, you need to define
a `lens_forward` function:

For example, suppose we have a LENS image below.
The `X` corresponds to the point at longitude=(FOV/2)º latitude=0º.
We flush the screen edge to this point to achieve the desired FOV zoom.

```
   -------------------------------------------------------------------------
   | LENS IMAGE                        ^                                   |
   |                                   |                                   |
   |                                   |                                   |
   |                 ------------------|-------------------                |
   |                 | SCREEN          |                  |                |
   |                 | (90º FOV)       |                  |                |
   |                 |                 |                  |                |
   |                 |                 0------------------X--------------> |
   |                 |                                    |\               |
   |                 |                                    | \              |
   |                 |                                    |  \ point at    |
   |                 --------------------------------------    lon = 45º   |
   |                                                           lat = 0º    |
   |                                                                       |
   |                                                                       |
   -------------------------------------------------------------------------
```

The process is similar when we want a vertical FOV:

```
   -------------------------------------------------------------------------
   | LENS IMAGE                        ^                                   |
   |                                   |                                   |
   |                                   |                                   |
   |                 ------------------X-------------------                |
   |                 | SCREEN          |\   point at      |                |
   |                 | (90º vertical)  | \  lon = 0º      |                |
   |                 |                 |    lat = 45º     |                |
   |                 |                 0---------------------------------> |
   |                 |                                    |                |
   |                 |                                    |                |
   |                 |                                    |                |
   |                 --------------------------------------                |
   |                                                                       |
   |                                                                       |
   |                                                                       |
   -------------------------------------------------------------------------
```

In the game, you can use the following commands to set the FOV:

- `f_fov` set horizontal FOV in degrees
- `f_vfov` set vertical FOV in degrees
- `fov` -- careful! this is the standard FOV only used when fisheye is off (i.e. `fisheye 0`)

### Using boundaries

We can also zoom the lens image such that its BOUNDARIES are flush with the screen.
This relies on the `lens_width` and `lens_height` variables.

In the game, you can use the following commands.  The terminology is borrowed
from the [background-size] property in CSS.

- `f_cover` image should be scaled as small as possible whiel still covering the screen.
- `f_contain` image should be scaled as large as possible while still being contained by the screen.

### Preferred Zoom

If you want your lens to have a preferred zoom applied each time it is loaded,
you can set the `onload` variable to the command string to apply that zoom.

For example, [fahey.lua](fahey.lua) defines:

```lua
onload = "f_contain"
```

## Usage

To use a lens in-game, enter the command:

```
f_lens <name>
```

Press `TAB` to help with auto-completion.

## References

Many of the projections here were taken from:

- [PROJ.4 Cartographic Projections Library](http://trac.osgeo.org/proj/)
