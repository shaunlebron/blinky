root2 = sqrt(2)

hsym = true
vsym = true
max_hfov = 360
max_vfov = 180
hfit_size = 2*sqrt(2)*2
vfit_size = sqrt(2)*2

function solveTheta(lat)
   local t = lat
   local dt
   repeat
      dt = -(t + sin(t) - pi*sin(lat))/(1+cos(t))
      t = t+dt
   until dt < 0.001
   return t/2
end

function lens_inverse(x,y)
   if x*x/8 + y*y/2 > 1 then
      return nil
   end
   local t = asin(y/root2);
   local lon = pi*x/(2*root2*cos(t))
   local lat = asin((2*t+sin(2*t))/pi)
   return latlon_to_ray(lat,lon)
end

function lens_forward(x,y,z)
   local lat,lon = ray_to_latlon(x,y,z)
   local t = solveTheta(lat)
   local x = 2*sqrt(2)/pi*lon*cos(t)
   local y = sqrt(2)*sin(t)
   return x,y
end
