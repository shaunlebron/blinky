max_fov = 360
max_vfov = 180

lens_width = 2*pi
lens_height = pi

onload = "f_fit"

function lens_inverse(x,y)
   if abs(y) > pi/2 or abs(x) > pi then
      return nil
   end
   local lon = x
   local lat = y
   return latlon_to_ray(lat,lon)
end

function lens_forward(x,y,z)
   local lat,lon = ray_to_latlon(x,y,z)
   local x = lon
   local y = lat
   return x,y
end
