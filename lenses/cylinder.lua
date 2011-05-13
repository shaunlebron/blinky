hsym = true
vsym = true
max_hfov = 360
max_vfov = 180
hfit_size = 2*pi

function xy_to_latlon(x,y)
   local lon = x
   local lat = atan(y)
   return lat, lon
end

function latlon_to_xy(lat,lon)
   local x = lon
   local y = tan(lat)
   return x,y
end

function xy_isvalid(x,y)
   return abs(x) <= pi
end
