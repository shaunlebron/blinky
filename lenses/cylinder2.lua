maxFovHeight = pi*2
maxFovWidth = pi

function xy_to_latlon(x,y)
   local lon = atan(x)
   local lat = y
   return lat, lon
end

function latlon_to_xy(lat,lon)
   local x = tan(lon)
   local y = lat
   return x,y
end

function xy_isvalid(x,y)
   return abs(y) <= pi
end
