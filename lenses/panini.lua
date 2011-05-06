map = "xy_to_latlon"
maxFovHeight = pi
d = 1
sf = 1 -- straightening factor

function xy_to_ray(x,y)
   -- for d=1
   local t = 4/(x*x+4)
   return x*t,y*t,2*t-1
end

function xy_to_latlon(x,y)
   local k = x*x/((d+1)*(d+1))
   local dscr = k*k*d*d - (k+1)*(k*d*d-1)
   if dscr < 0 then
      return nil
   end
   local clon = (-k*d+sqrt(dscr))/(k+1)
   local S = (d+1)/(d+clon)
   local lon = atan2(x,S*clon)
   local lat = atan2(y,S)
   return lat,lon
end

function latlon_to_xy(lat,lon)
   local S = (d+1)/(d+cos(lon))
   local x = S*sin(lon)
   local y = S*tan(lat)
   --local ys = (y / cos(lon))*sf + y*1-sf)
   return x, y
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
   --return 2*tan(fov*0.25) / (frame*0.5)
end
