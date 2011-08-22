Cl = 0.000952426
Cp = 0.162388
C12 = 0.08333333333333333

max_hfov = 360
max_vfov = 180

function lens_forward(x,y,z)
   local lat,lon = ray_to_latlon(x,y,z)

   local t = lat*lat
   local y = lat * (1 + t*C12)
   local x = lon * (1 - Cp*t)
   t = lon*lon
   x = x * (0.87 - Cl * t*t)
   return x,y
end

local x,y = lens_forward(latlon_to_ray(0,pi))
hfit_size = 2*abs(x)
x,y = lens_forward(latlon_to_ray(pi/2,0))
vfit_size = 2*abs(y)
