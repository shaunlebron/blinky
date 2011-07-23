
plates = {
   {{-cos(pi/6),0,sin(pi/6)},{0,1,0}, 120},
   {{cos(pi/6),0,sin(pi/6)},{0,1,0}, 120},
   {{0,0,-1},{0,1,0}, 120}
}

function ray_to_plate(x,y,z)
   local plate,u,v
   local lat,lon = ray_to_latlon(x,y,z)

   if abs(lat) > pi/3 then
      return nil
   end

   local c = cos(pi/3)
   local s = sin(pi/3)
   local left = 0
   local right = 1
   local back = 2

   local a

   if abs(lon) > pi/3 then
      plate = back
      a = pi
   elseif lon > 0 then
      plate = right
      a = pi/3
   else
      plate = left
      a = -pi/3
   end

   x,z = x*cos(a)-z*sin(a), x*sin(a)+z*cos(a)
   local dist = 0.5 / tan(pi/3)
   u = x/z*dist + 0.5
   v = -y/z*dist + 0.5

   if u < 0 or u > 1 or v < 0 or v > 1 then
      return nil
   else
      return plate, u, v
   end
end
