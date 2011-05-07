function xy_to_latlon(x,y)
   local lon = x
   local lat = 5/4*atan(sinh(4/5*y))
   return lat,lon
end

function latlon_to_xy(lat,lon)
   local x = lon
   local y = 1.25*log(tan(0.25*pi+0.4*lat))
   return x,y
end

function init(fov,width,height,frame)
   local x,y
   x,y = latlon_to_xy(pi*0.5,0)
   maxy = y
end

function xy_isvalid(x,y)
   return abs(y) <= maxy and abs(x) <= pi
end
