
hsym = true
vsym = true
max_hfov = 360
max_vfov = 180
hfit_size = pi*2
vfit_size = pi

function lens_forward(x,y,z)
   local lat,lon = ray_to_latlon(x,y,z)
   local x = lon*sqrt(1-3*lat*lat/(pi*pi))
   local y = lat
   return x,y
end
