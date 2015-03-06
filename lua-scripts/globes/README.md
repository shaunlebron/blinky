# GLOBES

The multiple camera views used to capture the environment are controlled by a
"globe" script.  It expects the following symbols, which we will go into more
detail:

- `plates` (array of [forward, up, fov] objects)
- `globe_plate` (optional function (x,y,z) -> plate index)


## Coordinate System:
    
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

__NOTE__: Plate coordinates are relative to the camera's frame.  They are NOT
absolute coordinates.

## Defining Plates

`plates` is an array, with each element containing a single camera's forward
vector, up vector, and fov. Together, the plates should form a complete globe
around the player.  For example, this is from [cube.lua](cube.lua):

```lua
plates = {
   -- forward     up           fov
   { { 0, 0, 1 }, { 0, 1, 0 }, 90 }, -- front
   { { 1, 0, 0 }, { 0, 1, 0 }, 90 }, -- right
   { { -1, 0, 0 }, { 0, 1, 0 }, 90 }, -- left
   { { 0, 0, -1 }, { 0, 1, 0 }, 90 }, -- back
   { { 0, 1, 0 }, { 0, 0, -1 }, 90 }, -- top
   { { 0, -1, 0 }, { 0, 0, 1 }, 90 } -- bottom
}
```

It creates the following plates:

```
          ---------
          |       |
          | TOP   |
          |       |
          ---------
--------- --------- --------- ---------
|       | |       | |       | |       |
| LEFT  | | FRONT | | RIGHT | | BACK  |
|       | |       | |       | |       |
--------- --------- --------- ---------
       ^  ---------
       |  |       |
      90º |BOTTOM |
       |  |       |
       v  ---------
          <--90º-->
```

## Resolving Plates

In order to resolve a given direction vector to a color, we must figure out
which plate plate that color will come from. The default behavior is to just
choose the plate whose center has the smallest angular distance from the
vector.  You can optionally define a `globe_plate` function to add your own
behavior.

`globe_plate` takes a vector, and returns a plate index.  For example,
[fast.lua](fast.lua) uses two plates pointed in the same direction (with 90º
and 160º).  The following function resolves all vectors inside the 90x90º
window to the 90º plate, and everything outside of it is resolved to the 160º
plate:

```lua
small = 0
big = 1
big_fov = 160

plates = {
{ {0,0,1}, {0,1,0}, 90 },
{ {0,0,1}, {0,1,0}, big_fov}
}

function globe_plate(x,y,z)
   if z <= 0 then
      return nil
   end

   local dist = 0.5 / tan(big_fov*pi/180/2)
   local size = 2*dist*tan(pi/4)

   local u = x/z*dist
   local v = y/z*dist

   local plate
   if abs(u) < size/2 and abs(v) < size/2 then
      return small
   else
      return big
   end
end
```

## Usage

To use a globe in-game, enter the command:

```
f_globe <name>
```

The available globes are:

- cube: front-facing cube
- cube_edge: edge-facing cube
- cube_corner: corner-facing cube
- trism: a triangular prism with 5 views
- tetra: a tetrahedron with 4 views
- fast:  2 overlaid views in the same direction (90 and 160 degrees)

