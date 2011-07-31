d = 1
gumbyScale = 0.75
gumbyScaleInv = 1.0/gumbyScale

hsym = true
vsym = true
max_hfov = 360
max_vfov = 180

function lens_inverse(x,y)
   local k = x*x/((d+1)*(d+1))
   local dscr = k*k*d*d - (k+1)*(k*d*d-1)
   local clon = (-k*d+sqrt(dscr))/(k+1)
   local S = (d+1)/(d+clon)
   local lon = atan2(x,S*clon)
   local lat = atan2(y,S)
   lon = lon*gumbyScaleInv
   lat = lat*gumbyScaleInv
   return latlon_to_ray(lat,lon)
end

function lens_forward(x,y,z)
   local lat,lon = ray_to_latlon(x,y,z)
   lon = lon*gumbyScale
   lat = lat*gumbyScale
   local S = (d+1)/(d+cos(lon))
   local x = S*sin(lon)
   local y = S*tan(lat)
   return x,y
end

local x,y = lens_forward(latlon_to_ray(pi/2,0))
vfit_size = y*2

x,y = lens_forward(latlon_to_ray(0,pi))
hfit_size = x*2
