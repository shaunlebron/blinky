
max_fov = 360
max_vfov = 180

lens_width = 3*pi/(2*pi)*sqrt(pi*pi/3)*2
lens_height = pi

onload = "f_fit"

function lens_forward(x,y,z)
   local lat,lon = ray_to_latlon(x,y,z)
   local x = 3*lon/(2*pi)*sqrt(pi*pi/3 - lat*lat)
   local y = lat
   return x,y
end
