
max_fov = 360
max_vfov = 180

lens_width = pi*2
lens_height = pi

onload = "f_contain"

function lens_forward(x,y,z)
   local lat,lon = ray_to_latlon(x,y,z)
   local x = lon * (1 + cos(lat))/2
   local y = lat
   return x,y
end
