map = "xy_to_latlon"
maxFovHeight = math.pi

function xy_to_latlon(x,y)

   if math.abs(y) > maxy then
      return nil
   end

   local lon = x
   local lat = 5/4*math.atan(math.sinh(4/5*y))
   if math.abs(lon) > math.pi then
      return nil
   end
   return lat,lon
end

function init(fov,width,height,frame)
   maxy = 5/4*math.log(math.tan(math.pi*0.25 + math.pi/5))
   return fov / frame;
end
