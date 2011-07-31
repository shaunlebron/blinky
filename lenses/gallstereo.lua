YF = 1.70710678118654752440
XF = 0.70710678118654752440
RYF = 0.58578643762690495119
RXF = 1.41421356237309504880

maxx = XF * pi
maxy = YF * tan(0.5*pi/2)

hsym = true
vsym = true
max_hfov = 360
max_vfov = 180
hfit_size = maxx*2
vfit_size = maxy*2

function lens_forward(x,y,z)
   if abs(x) > maxx or abs(y) > maxy then
      return nil
   end
   local lat,lon = ray_to_latlon(x,y,z)
   local x = XF * lon
   local y = YF * tan(0.5 * lat)
   return x,y
end

function lens_inverse(x,y)
   local lon = RXF * x
   local lat = 2 * atan(y * RYF)
   return latlon_to_ray(lat,lon)
end
