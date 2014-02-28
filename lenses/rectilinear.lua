hsym = true
vsym = true
max_hfov = 180
max_vfov = 180

function lens_inverse(x,y)
   local r = sqrt(x*x+y*y)

   local theta = atan(r)
   
   local s = sin(theta)
   return x/r*s, y/r*s, cos(theta)
end

function lens_forward(x,y,z)
   local theta = acos(z)

   local r = tan(theta)

   local c = r/sqrt(x*x+y*y)
   return x*c, y*c
end

--function lens_forward(x,y,z)
--   local lat,lon = ray_to_latlon(x,y,z)
--   local cosc = cos(lat)*cos(lon)
--   local x = cos(lat)*sin(lon)/cosc
--   local y = sin(lat)/cosc
--   return x,y
--end
--
--function lens_inverse(x,y)
--   local p = sqrt(x*x+y*y)
--   local c = atan(p)
--   local lat = asin(y*sin(c)/p)
--   local lon = atan2(x*sin(c),p*cos(c))
--   return latlon_to_ray(lat,lon)
--end
