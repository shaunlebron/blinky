top = 0
left = 1
right = 2
back = 3

-- degrees
d120 = tau/3
d60 = d120 / 2
d30 = d60 / 2

-- tetrahedron dimensions
r = 1 -- face to vertex
s = 2*r*sin(d60) -- side length
h = sqrt(s*s-r*r) -- face to vertex opposite face
theta = acos(r/s) -- face to vertex to vertex opposite face
c = s/2/sin(theta) -- center to vertex
d = s/2/tan(theta) -- center to edge
e = r*cos(d60) -- face to edge
f = h-c -- center to face

fov = 2*atan(s/2/f)

local y,z
y = e - e*e/(r+e)
z =-f + h*e/(r+e)

plates = {
   {
      {0,y/e,z/e},
      {0,(e-y)/e,(-f-z)/e}, 
      fov
   },
   {
      {-y/e*sin(d120),y/e*cos(d120),z/e},
      {-(e-y)/e*sin(d120),(e-y)/e*cos(d120),(-f-z)/e}, 
      fov
   },
   {
      {-y/e*sin(-d120),y/e*cos(-d120),z/e},
      {-(e-y)/e*sin(-d120),(e-y)/e*cos(-d120),(-f-z)/e}, 
      fov
   },
   {{0,0,-1},{0,1,0},fov}
}

function ray_to_plate(x,y,z)

   -- determine the plate (plate forward vector closest to ray)
   local plate = 0
   local mina = tau
   local i
   for i in 1,4 do
      -- note: we are assuming the ray (x,y,z) is normalized. but is it?
      local a = abs(acos(x*plate[i][1][1]+y*plate[i][1][2]+z*plate[i][1][3]))
      if a < mina then
         mina = a
         plate = i-1
      end
   end

   -- TODO: rotate ray such that the plate's forward is (0,0,1) and up is (0,1,0)
   --       (may require quaternion rotation)
end
