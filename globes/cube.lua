front = 0 
right = 1
left = 2
back = 3
top = 4
bottom = 5

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
plates[2] = {
   -- forward
   -- up
   -- aspect
   -- hfov
}
plates[3] = {
   -- forward
   -- up
   -- aspect
   -- hfov
}
plates[4] = {
   -- forward
   -- up
   -- aspect
   -- hfov
}
plates[5] = {
   -- forward
   -- up
   -- aspect
   -- hfov
}

-- inverse
function ray_to_globe(x,y,z)
   local plate
   local ax = abs(x)
   local ay = abs(y)
   local az = abs(z)

   -- determine which plate to use
   if ax > ay then
      if ax > az then
         if x > 0 then
            plate = right
         else
            plate = left
         end
      else
         if z > o then
            plate = front
         else
            plate = back
         end
      end
   else
      if ay > az then
         if y > 0 then
            plate = top
         else
            plate = bottom
         end
      else
         if z > 0 then
            plate = front
         else
            plate = back
         end
      end
   end

   -- determine the coordinate
   local u,v
   if plate == front then
      u = x/z
      v = y/z
   elseif plate == behind then
      u = -x/-z
      v = -y/-z
   elseif plate == left then
      u = z/-x
      v = -y/-x
   elseif plate == right then
      u = -z/x
      v = -y/x
   elseif plate == bottom then
      u = x/-y
      v = -z/-y
   elseif plate == top then
      u = x/y
      v = z/y
   else
      return nil
   end

   return plate, u/2+0.5, v/2+0.5
end

-- forward
function globe_to_ray(plate, u, v)
   local x = u - 0.5
   local y = 0.5 - v
   local z = 0.5

   if plate == front then
      x,y,z = x,y,z
   elseif plate == back then
      x,y,z = -x,y,-z
   elseif plate == left then
      x,y,z = -z, y, x
   elseif plate == right then
      x,y,z = z,y,-x
   elseif plate == top then
      x,y,z = x,z,-y
   elseif plate == bottom then
      x,y,z = x,-z,y
   else
      return nil
   end

   return x,y,z
end
