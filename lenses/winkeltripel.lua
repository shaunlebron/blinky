-- cos of the standard parallel at 50 degrees 28'
clat0 = 2/pi

hsym = true
vsym = true
max_hfov = 360
max_vfov = 180

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

local x,y = latlon_to_xy(pi/2,0)
vfit_size = 2*y

x,y = latlon_to_xy(0,pi)
hfit_size = 2*x
