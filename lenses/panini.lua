d = 1

hsym = true
vsym = true
max_hfov = 360
max_vfov = 180


function xy_to_latlon(x,y)
   local k = x*x/((d+1)*(d+1))
   local dscr = k*k*d*d - (k+1)*(k*d*d-1)
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
   return x,y
end

--function xy_to_ray(x,y)
--   -- for d=1 only
--   local t = 4/(x*x+4)
--   return x*t,y*t,2*t-1
--end

