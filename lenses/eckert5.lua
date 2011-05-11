
function latlon_to_xy(lat,lon)
   local x = lon * (1 + cos(lat))/2
   local y = lat
   return x,y
end
