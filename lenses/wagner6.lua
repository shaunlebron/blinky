
max_hfov = 360
max_vfov = 180

lens_width = pi*2
lens_height = pi

onload = "fit"

function lens_forward(x,y,z)
   local lat,lon = ray_to_latlon(x,y,z)
   local x = lon*sqrt(1-3*lat*lat/(pi*pi))
   local y = lat
   return x,y
end
