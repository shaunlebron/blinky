YF = 1.70710678118654752440
XF = 0.70710678118654752440
RYF = 0.58578643762690495119
RXF = 1.41421356237309504880

function latlon_to_xy(lat,lon)
   local x = XF * lon
   local y = YF * tan(0.5 * lat)
   return x,y
end

function xy_to_latlon(x,y)
   local lon = RXF * x
   local lat = 2 * atan(y * RYF)
   return lat,lon
end
