-- Mercator Projection

-- horizontal and vertical symmetry
hsym = true
vsym = true

-- FOV bounds
max_hfov = 360
max_vfov = 180

-- width of the image
hfit_size = 2*pi

-- inverse mapping (screen to environment)
function xy_to_latlon(x,y)
   local lon = x
   local lat = atan(sinh(y))
   return lat, lon
end

function latlon_to_xy(lat,lon)
   local x = lon
   local y = log(tan(pi*0.25+lat*0.5))
   return x,y
end

function xy_isvalid(x,y)
   return abs(x) <= pi
end
