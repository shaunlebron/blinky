
function latlon_to_xy(lat,lon)
   if lat == 0 then
      return lon,0
   end
   local x = 1/tan(lat)*sin(lon*sin(lat))
   local y = lat + 1/tan(lat)*(1 - cos(lon*sin(lat)))
   return x,y
end
