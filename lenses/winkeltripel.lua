-- cos of the standard parallel at 50 degrees 28'
clat0 = 2/pi

function latlon_to_xy(lat,lon)
   local clat = cos(lat)
   local temp = clat*cos(lon*0.5)
   local D = acos(temp)
   local C = 1 - temp*temp
   temp = D/sqrt(C)

   local x = 0.5 * (2*temp*clat*sin(lon*0.5)+lon*clat0)
   local y = 0.5 * (temp*sin(lat) + lat)

   return x,y
end

