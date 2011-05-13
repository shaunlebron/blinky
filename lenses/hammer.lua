hsym = true
vsym = true
max_hfov = 360
max_vfov = 180
hfit_size = 2*sqrt(2)*2
vfit_size = sqrt(2)*2

function xy_to_latlon(x,y)
   local z = sqrt(1-0.0625*x*x-0.25*y*y)
   local lon = 2*atan(z*x/(2*(2*z*z-1)))
   local lat = asin(z*y)
   return lat,lon
end

function latlon_to_xy(lat,lon)
   local x = 2*sqrt(2)*cos(lat)*sin(lon*0.5) / sqrt(1+cos(lat)*cos(lon*0.5))
   local y = sqrt(2)*sin(lat) / sqrt(1+cos(lat)*cos(lon*0.5))
   return x,y
end

function xy_isvalid(x,y)
   return x*x/8+y*y/2 <= 1
end
