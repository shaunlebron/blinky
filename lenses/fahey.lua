XR = 0.819152 * pi
YR = 1.819152

hsym = true
vsym = true
max_hfov = 360
max_vfov = 180
hfit_size = XR*2
vfit_size = YR*2

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
