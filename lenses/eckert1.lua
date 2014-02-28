FC = 0.92131773192356127802
RP = 0.31830988618379067154

hsym = true
vsym = true
max_hfov = 360
max_vfov = 180
hfit_size = FC * pi * 2
vfit_size = FC * pi

function lens_forward(x,y,z)
   local lat,lon = ray_to_latlon(x,y,z)
   local x = FC * lon * (1 - RP * abs(lat))
   local y = FC * lat
   return x,y
end
