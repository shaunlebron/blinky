map = 0

function inverse(x,y)

   local lon = x
   local lat = math.atan(y)
   if math.abs(lat) > math.pi*0.5 or math.abs(lon) > math.pi then
      return 0
   end

   local clat = math.cos(lat)

   return {
      math.sin(lon)*clat,
      math.sin(lat),
      math.cos(lon)*clat}
end

function init(fov,width,height,frame)

   if frame == width then
      if fov > math.pi*2 then
         return 0
      end
      scale = fov / frame;
   elseif frame == height then
      if fov > math.pi then
         return 0
      end
      scale = fov / frame;
   else
      return 0
   end
   return 1
end
