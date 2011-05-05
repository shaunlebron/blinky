map = "xy_to_latlon"
maxFovHeight = math.pi

function xy_to_latlon(x,y)
   local lon = x
   if math.abs(lon) > math.pi then
      return nil
   end
   local lat = math.atan(math.sinh(y))
   return lat, lon
end

function init(fov,width,height,frame)
   return fov / frame;
end
