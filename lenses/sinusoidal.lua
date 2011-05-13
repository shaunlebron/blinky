
hsym = true
vsym = true
max_hfov = 360
max_vfov = 180
hfit_size = 2*pi
vfit_size = pi

function latlon_to_xy(lat,lon)
   local x = lon*cos(lat)
   local y = lat
   return x,y
end
