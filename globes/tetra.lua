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

fovr = 2*atan(s/2/f)
fovd = fovr * 180 / pi

local y,z
y = e - e*e/(r+e)
z =-f + h*e/(r+e)

plates = {
   {
      {0,y/e,z/e},
      {0,(e-y)/e,(-f-z)/e}, 
      fovd
   },
   {
      {-y/e*sin(d120),y/e*cos(d120),z/e},
      {-(e-y)/e*sin(d120),(e-y)/e*cos(d120),(-f-z)/e}, 
      fovd
   },
   {
      {-y/e*sin(-d120),y/e*cos(-d120),z/e},
      {-(e-y)/e*sin(-d120),(e-y)/e*cos(-d120),(-f-z)/e}, 
      fovd
   },
   {{0,0,-1},{0,1,0},fovd}
}

function ray_to_plate(x,y,z)

   -- determine the plate (plate forward vector closest to ray)
   local plate = 1
   local mina = tau
   local i
   for i=1,4 do
      -- note: we are assuming the ray (x,y,z) is normalized. but is it?
      local a = abs(acos(x*plates[i][1][1]+y*plates[i][1][2]+z*plates[i][1][3]))
      if a < mina then
         mina = a
         plate = i
      end
   end

   -- rotate ray such that the plate's forward is (0,0,1) and up is (0,1,0)
   local u = plates[plate][2]
   local f = plates[plate][1]
   local r = {
      u[2]*f[3]-u[3]*f[2],
      u[1]*f[3]-u[3]*f[1],
      u[1]*f[2]-u[2]*f[1]
   }
   local nx = r[1]*x + r[2]*y + r[3]*z
   local ny = u[1]*x + u[2]*y + u[3]*z
   local nz = f[1]*x + f[2]*y + f[3]*z

   -- get texture coordinates
   local dist = 0.5 / tan(fovr/2)
   local u = nx/nz*dist+0.5
   local v = ny/nz*dist+0.5

   return plate-1, u, v
end
