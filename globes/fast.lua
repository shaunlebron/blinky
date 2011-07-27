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
