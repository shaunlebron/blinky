
function latlon_to_xy(lat,lon)
   local x = 3*lon/(2*pi)*sqrt(pi*pi/3 - lat*lat)
   local y = lat
   return x,y
end
