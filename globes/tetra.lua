-- degrees
local d120 = tau/3
local d60 = d120 / 2
local d30 = d60 / 2

-- tetrahedron dimensions
local r = 1 -- face to vertex
local s = 2*r*sin(d60) -- side length
local h = sqrt(s*s-r*r) -- face to vertex opposite face
local theta = acos(r/s) -- face to vertex to vertex opposite face
local c = s/2/sin(theta) -- center to vertex
local d = s/2/tan(theta) -- center to edge
local e = r*cos(d60) -- face to edge
local f = h-c -- center to face

-- compute fov
local fovr = 2*atan(r/f)
local fovd = fovr * 180 / pi + 1 -- +1 to get rid of the hole in the center
print(fovd)

local y = e - e*e/(r+e)
local z =-f + h*e/(r+e)

plates = {
   { -- bottom
      {0,-y/f,z/f},
      {0,-(e-y)/e,(-f-z)/e}, 
      fovd
   },
   { -- right
      {y/f*sin(d120),-y/f*cos(d120),z/f},
      {(e-y)/e*sin(d120),-(e-y)/e*cos(d120),(-f-z)/e}, 
      fovd
   },
   { -- left
      {y/f*sin(-d120),-y/f*cos(-d120),z/f},
      {(e-y)/e*sin(-d120),-(e-y)/e*cos(-d120),(-f-z)/e}, 
      fovd
   },
   {{0,0,-1},{0,-1,0},fovd} -- back
}

