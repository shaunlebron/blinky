
function latlon_to_xy(lat,lon)
   local x = lon * (2/pi + cos(lat))/2
   local y = lat
   return x,y
end
