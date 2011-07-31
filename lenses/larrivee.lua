
hsym = true
vsym = true
max_hfov = 360
max_vfov = 180
hfit_size = 2*pi
vfit_size = pi/2 / cos(pi/2/2) * 2

function lens_forward(x,y,z)
   local lat,lon = ray_to_latlon(x,y,z)
   local x = (0.5 + 0.5*sqrt(cos(lat)))*lon
   local y = lat / (cos(lat/2)*cos(lon/6))
   return x,y
end
