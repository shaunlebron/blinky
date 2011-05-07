d = 1
gumbyScale = 0.75
gumbyScaleInv = 1.0/gumbyScale

function xy_to_latlon(x,y)
   local k = x*x/((d+1)*(d+1))
   local dscr = k*k*d*d - (k+1)*(k*d*d-1)
   local clon = (-k*d+sqrt(dscr))/(k+1)
   local S = (d+1)/(d+clon)
   local lon = atan2(x,S*clon)
   local lat = atan2(y,S)
   lon = lon*gumbyScaleInv
   lat = lat*gumbyScaleInv
   return lat,lon
end

function latlon_to_xy(lat,lon)
   lon = lon*gumbyScale
   lat = lat*gumbyScale
   local S = (d+1)/(d+cos(lon))
   local x = S*sin(lon)
   local y = S*tan(lat)
   return x,y
end
