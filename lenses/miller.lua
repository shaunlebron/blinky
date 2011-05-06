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

function latlon_to_xy(lat,lon)
   local x = lon
   local y = 1.25*log(tan(0.25*pi+0.4*lat))
   return x,y
end

function init(fov,width,height,frame)
   local x,y
   x,y = latlon_to_xy(pi*0.5,0)
   maxy = y

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
