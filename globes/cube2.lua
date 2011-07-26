front = 0 
right = 1
left = 2
back = 3
top = 4
bottom = 5

-- 6 plates
plates = {
{ { 0, 0, 1 }, { 0, 1, 0 }, 90 },
{ { 1, 0, 0 }, { 0, 1, 0 }, 90 },
{ { -1, 0, 0 }, { 0, 1, 0 }, 90 },
{ { 0, 0, -1 }, { 0, 1, 0 }, 90 },
{ { 0, 1, 0 }, { 0, 0, -1 }, 90 },
{ { 0, -1, 0 }, { 0, 0, 1 }, 90 }
}

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

function ray_to_plate(x,y,z)

   -- determine the plate (plate forward vector closest to ray)
   local plate = 1
   local mina = tau
   local i
   for i=1,6 do
      -- note: we are assuming the ray (x,y,z) is normalized. but is it?
      local a = abs(acos(x*plates[i][1][1]+y*plates[i][1][2]+z*plates[i][1][3]))
      if a < mina then
         mina = a
         plate = i
      end
   end

   -- get the current ray vector as a linear combination of the
   --    plate's right,up,forward vectors
   local u = plates[plate][2]
   local f = plates[plate][1]
   local r = {
      u[2]*f[3]-u[3]*f[2],
      -(u[1]*f[3]-u[3]*f[1]),
      u[1]*f[2]-u[2]*f[1]
   }
   local nx = r[1]*x + r[2]*y + r[3]*z
   local ny = u[1]*x + u[2]*y + u[3]*z
   local nz = f[1]*x + f[2]*y + f[3]*z

   -- get texture coordinates
   local dist = 0.5 / tan(fovr/2)
   local u = nx/nz*dist+0.5
   local v = -ny/nz*dist+0.5

   if u < 0 or u > 1 or v <0 or v > 1 then
      return nil
   end

   return plate-1, u, v
end
