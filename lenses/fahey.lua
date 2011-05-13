XR = 0.819152 * pi
YR = 1.819152

hsym = true
vsym = true
max_hfov = 360
max_vfov = 180
hfit_size = XR*2
vfit_size = YR*2

function latlon_to_xy(lat,lon)
   local x = tan(0.5 * lat);
   local y = 1.819152 * x;
   x = 0.819152 * lon * sqrt(1-x*x);
   return x,y
end

function xy_to_latlon(x,y)
   y = y / 1.819152
   lat = 2 * atan(y)
   y = 1 - y*y
   lon = x / (0.819152 * sqrt(y))
   return lat,lon
end

function xy_isvalid(x,y)
   return x*x/(XR*XR) + y*y/(YR*YR) < 1
end
