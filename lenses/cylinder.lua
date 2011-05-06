map = "xy_to_latlon"
maxFovHeight = pi

function xy_to_latlon(x,y)

   local lon = x
   local lat = atan(y)
   if abs(lat) > pi*0.5 or abs(lon) > pi then
      return nil
   end

   return lat, lon
end

function latlon_to_xy(lat,lon)
   local x = lon
   local y = tan(lat)
   return x,y
end

function init(fov,width,height,frame)
   local x,y
   if frame == width then
      x,y = latlon_to_xy(0,fov*0.5)
      return x / (frame*0.5)
   elseif frame == height then
      x,y = latlon_to_xy(fov*0.5,0)
      return y / (frame*0.5)
   else
      return nil
   end
end
