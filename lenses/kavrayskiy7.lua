
hsym = true
vsym = true
max_hfov = 360
max_vfov = 180
hfit_size = 3*pi/(2*pi)*sqrt(pi*pi/3)*2
vfit_size = pi

function lens_forward(x,y,z)
   local lat,lon = ray_to_latlon(x,y,z)
   local x = 3*lon/(2*pi)*sqrt(pi*pi/3 - lat*lat)
   local y = lat
   return x,y
end
