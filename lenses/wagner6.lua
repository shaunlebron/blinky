
function latlon_to_xy(lat,lon)
   local x = lon*sqrt(1-3*lat*lat/(pi*pi))
   local y = lat
   return x,y
end
