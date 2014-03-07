
max_hfov = 360
max_vfov = 180

lens_width = 2*pi
lens_height = pi/2 / cos(pi/2/2) * 2

onload = "fit"

function lens_forward(x,y,z)
   local lat,lon = ray_to_latlon(x,y,z)
   local x = (0.5 + 0.5*sqrt(cos(lat)))*lon
   local y = lat / (cos(lat/2)*cos(lon/6))
   return x,y
end
