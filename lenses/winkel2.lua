
hsym = true
vsym = true
max_hfov = 360
max_vfov = 180
hfit_size = pi/2*(2/pi+1)*2
vfit_size = pi

function latlon_to_xy(lat,lon)
   local x = lon/2*(2/pi + sqrt(pi*pi - 4*lat*lat)/pi)
   local y = lat
   return x,y
end
