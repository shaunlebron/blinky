
max_hfov = 360
max_vfov = 180

lens_width = 2*pi
lens_height = pi

onload = "fit"

function lens_forward(x,y,z)
   local lat,lon = ray_to_latlon(x,y,z)
   local x = lon*cos(lat)
   local y = lat
   return x,y
end
