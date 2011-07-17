small = 0
big = 1
big_fov = 160

-- 6 plates
plates = {}

plates[0] = {
   -- forward
   -- up
   -- aspect
   -- hfov
}
plates[1] = {
   -- forward
   -- up
   -- aspect
   -- hfov
}

-- inverse
function ray_to_globe(x,y,z)
   if z < 0 then
      return nil
   end

   local dist = 0.5 / tan(big_fov/2)
   local size = 2*dist*tan(pi/4)

   local u = x/z*dist
   local v = y/z*dst

   local plate
   if abs(u) < size/2 and abs(v) < size/2 then
      u = u/(size/2)
      v = v/(size/2)
      plate = small
   else
      plate = big
   end

   u = u + 0.5
   v = -v + 0.5

   if u < 0 or u > 1 or v < 0 or v > 1 then
      return nil
   end

   return plate,u,v
end

-- forward
function globe_to_ray(plate, u, v)
   local x = u - 0.5
   local y = 0.5 - v
   local z

   if plate == big then
      z = 0.5 / tan(big_fov/2)
   elseif plate == small then
      z = 0.5
   else
      return nil
   end

   return x,y,z
end
