
function latlon_to_xy(lat,lon)
   local x = lon/2*(2/pi + sqrt(pi*pi - 4*lat*lat)/pi)
   local y = lat
   return x,y
end
