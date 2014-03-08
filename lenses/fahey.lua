XR = 0.819152 * pi
YR = 1.819152

max_fov = 360
max_vfov = 180

lens_width = XR*2
lens_height = YR*2

onload = "f_fit"

function lens_forward(x,y,z)
   local lat,lon = ray_to_latlon(x,y,z)
   local x = tan(0.5 * lat);
   local y = 1.819152 * x;
   x = 0.819152 * lon * sqrt(1-x*x);
   return x,y
end

function lens_inverse(x,y)
   if x*x/(XR*XR) + y*y/(YR*YR) >= 1 then
      return nil
   end
   y = y / 1.819152
   lat = 2 * atan(y)
   y = 1 - y*y
   lon = x / (0.819152 * sqrt(y))
   return latlon_to_ray(lat,lon)
end
