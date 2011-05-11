
function latlon_to_xy(lat,lon)
   local x = (0.5 + 0.5*sqrt(cos(lat)))*lon
   local y = lat / (cos(lat/2)*cos(lon/6))
   return x,y
end
