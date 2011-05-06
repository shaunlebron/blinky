map = "xy_to_latlon"
maxFovHeight = pi

function xy_to_latlon(x,y)
   if x*x/8+y*y/2 > 1 then
      return nil
   end

   local z = sqrt(1-0.0625*x*x-0.25*y*y)
   local lon = 2*atan(z*x/(2*(2*z*z-1)))
   local lat = asin(z*y)
   return lat,lon
end

function latlon_to_xy(lat,lon)
   local x = 2*sqrt(2)*cos(lat)*sin(lon*0.5) / sqrt(1+cos(lat)*cos(lon*0.5))
   local y = sqrt(2)*sin(lat) / sqrt(1+cos(lat)*cos(lon*0.5))
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
