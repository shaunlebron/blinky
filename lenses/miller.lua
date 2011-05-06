map = "xy_to_latlon"
maxFovHeight = pi

function xy_to_latlon(x,y)

   if abs(y) > maxy then
      return nil
   end

   local lon = x
   local lat = 5/4*atan(sinh(4/5*y))
   if abs(lon) > pi then
      return nil
   end
   return lat,lon
end

function init(fov,width,height,frame)
   maxy = 5/4*log(tan(pi*0.25 + pi/5))
   return fov / frame;
end
