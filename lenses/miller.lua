maxy = 1.25*log(tan(0.25*pi+0.4*pi*0.5))

max_fov = 360
max_vfov = 180

lens_width = 2*pi
lens_height = maxy*2

onload = "f_fit"

function lens_inverse(x,y)
   if abs(y) > maxy or abs(x) > pi then
      return nil
   end
   local lon = x
   local lat = 5/4*atan(sinh(4/5*y))
   return latlon_to_ray(lat,lon)
end

function lens_forward(x,y,z)
   local lat,lon = ray_to_latlon(x,y,z)
   local x = lon
   local y = 1.25*log(tan(0.25*pi+0.4*lat))
   return x,y
end
