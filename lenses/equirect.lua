map = "xy_to_latlon"
maxFovHeight = pi

function xy_to_latlon(x,y)
   local lon = x
   local lat = y
   if abs(lat) > pi*0.5 or abs(lon) > pi then
      return nil
   end

   return lat, lon
end

function init(fov,width,height,frame)
   return fov / frame;
end
