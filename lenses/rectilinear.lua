hsym = true
vsym = true
max_hfov = 180
max_vfov = 180

function r_to_theta(r)
   return atan(r)
end

function theta_to_r(theta)
   return tan(theta)
end

function latlon_to_xy(lat,lon)
   local cosc = cos(lat)*cos(lon)
   local x = cos(lat)*sin(lon)/cosc
   local y = sin(lat)/cosc
   return x,y
end

function xy_to_latlon(x,y)
   local p = sqrt(x*x+y*y)
   local c = atan(p)
   local lat = asin(y*sin(c)/p)
   local lon = atan2(x*sin(c),p*cos(c))
   return lat,lon
end
