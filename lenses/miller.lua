maxy = 1.25*log(tan(0.25*pi+0.4*pi*0.5))

hsym = true
vsym = true
max_hfov = 360
max_vfov = 180
hfit_size = 2*pi
vfit_size = maxy*2

function xy_to_latlon(x,y)
   local lon = x
   local lat = 5/4*atan(sinh(4/5*y))
   return lat,lon
end

function latlon_to_xy(lat,lon)
   local x = lon
   local y = 1.25*log(tan(0.25*pi+0.4*lat))
   return x,y
end

function xy_isvalid(x,y)
   return abs(y) <= maxy and abs(x) <= pi
end
