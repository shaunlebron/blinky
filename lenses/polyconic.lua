
hsym = true
vsym = true
max_hfov = 360
max_vfov = 180

function lens_forward(x,y,z)
   local lat,lon = ray_to_latlon(x,y,z)
   if lat == 0 then
      return lon,0
   end
   local x = 1/tan(lat)*sin(lon*sin(lat))
   local y = lat + 1/tan(lat)*(1 - cos(lon*sin(lat)))
   return x,y
end
