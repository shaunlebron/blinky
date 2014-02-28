scale = 0.5

function latlon_to_xy(lat,lon)
   --lon = lon * scale
   local cosc = cos(lat)*cos(lon)
   local x = cos(lat)*sin(lon)/cosc
   local y = sin(lat)/cosc
   return x,y
end

function xy_to_latlon(x,y)
   local p = sqrt(x*x+y*y)
   local c = atan(p)
   local lat = asin(y*sin(c)/p)
   local lon = atan2(x*sin(c),p*cos(c))
   return lat,lon
end

--function xy_to_latlon(x0,y0)
--   local len = sqrt(x0*x0+y0*y0+1)
--   local z = 1/len
--   local x = x0 * z
--   local y = y0 * z
--   local lon = atan2(x,z)
--   local lat = atan2(y, sqrt(x*x+z*z))
--   lon = lon / scale
--   return lat,lon
--end
