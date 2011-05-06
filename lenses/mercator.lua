map = "xy_to_latlon"
maxFovHeight = pi

function xy_to_latlon(x,y)
   local lon = x
   if abs(lon) > pi then
      return nil
   end
   local lat = atan(sinh(y))
   return lat, lon
end

function init(fov,width,height,frame)
   return fov / frame;
end
