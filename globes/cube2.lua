-- a cubemap rotated with a corner at the center view

plates = {
{ { 0, 0, 1 }, { 0, 1, 0 }, 90 }, -- front
{ { 1, 0, 0 }, { 0, 1, 0 }, 90 }, -- right
{ { -1, 0, 0 }, { 0, 1, 0 }, 90 }, -- left
{ { 0, 0, -1 }, { 0, 1, 0 }, 90 }, -- back
{ { 0, 1, 0 }, { 0, 0, -1 }, 90 }, -- top
{ { 0, -1, 0 }, { 0, 0, 1 }, 90 } -- bottom
}

-- rotate cube

fovr = pi/2

local i
for i=1,6 do
   local x,y,z
   local a = pi/4

   local p = plates[i][1]
   x = p[1]
   z = p[3]
   p[1] = x*cos(a)-z*sin(a)
   p[3] = x*sin(a)+z*cos(a)
   y = p[2]
   z = p[3]
   p[2] = y*cos(a)-z*sin(a)
   p[3] = y*sin(a)+z*cos(a)

   local p = plates[i][2]
   x = p[1]
   z = p[3]
   p[1] = x*cos(a)-z*sin(a)
   p[3] = x*sin(a)+z*cos(a)
   y = p[2]
   z = p[3]
   p[2] = y*cos(a)-z*sin(a)
   p[3] = y*sin(a)+z*cos(a)
end
