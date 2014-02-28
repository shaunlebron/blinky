-- Mercator Projection

-- horizontal and vertical symmetry
hsym = true
vsym = true

-- FOV bounds
max_hfov = 360
max_vfov = 180

-- horizontal fit size
hfit_size = 2*pi

-- inverse mapping (screen to environment)
function lens_inverse(x,y)
   if abs(x) > pi then
      return nil
   end
   local lon = x
   local lat = atan(sinh(y))
   return latlon_to_ray(lat,lon)
end

-- forward mapping (environment to screen)
function lens_forward(x,y,z)
   local lat,lon = ray_to_latlon(x,y,z)
   local x = lon
   local y = log(tan(pi*0.25+lat*0.5))
   return x,y
end
