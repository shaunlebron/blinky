
function latlon_to_xy(lat,lon)
   local x = lon*cos(lat)
   local y = lat
   return x,y
end
