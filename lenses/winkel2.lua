
max_fov = 360
max_vfov = 180

lens_width = pi/2*(2/pi+1)*2
lens_height = pi

onload = "f_contain"

function lens_forward(x,y,z)
   local lat,lon = ray_to_latlon(x,y,z)
   local x = lon/2*(2/pi + sqrt(pi*pi - 4*lat*lat)/pi)
   local y = lat
   return x,y
end
