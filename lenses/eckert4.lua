
function solveTheta(lat)
   local t = lat/2;
   local dt = 0
   for i=1,20 do
   --repeat
      dt = -(t + sin(t)*cos(t) + 2*sin(t) - (2+pi*0.5)*sin(lat))/(2*cos(t)*(1+cos(t)))
      t = t+dt
   --until dt < 0.001
   end
   return t
end

function get_max_x(y,lat)
   if y ~= lasty then
      local t = solveTheta(abs(lat))
      maxx = 2/sqrt(pi*(4+pi))*pi*(1+cos(t))
      lasty = y
   end
   return maxx
end

function lens_inverse(x,y)
   local t = asin(y/2*sqrt((4+pi)/pi))
   local lat = asin((t+sin(t)*cos(t)+2*sin(t))/(2+pi*0.5))
   local lon = sqrt(pi*(4+pi))*x/(2*(1+cos(t)))

   if abs(y) > maxy or abs(x) > get_max_x(y,lat) then
      return nil
   end
   return latlon_to_ray(lat,lon)
end

function lens_forward(x,y,z)
   local lat,lon = ray_to_latlon(x,y,z)
   local t = solveTheta(lat)
   local x = 2/sqrt(pi*(4+pi))*lon*(1+cos(t))
   local y = 2*sqrt(pi/(4+pi))*sin(t)
   return x,y
end

local t = solveTheta(pi*0.5)
maxy = 2*sqrt(pi/(4+pi))*sin(t)

hsym = true
vsym = true
max_hfov = 360
max_vfov = 180

t = solveTheta(0)
hfit_size = 2/sqrt(pi*(4+pi))*pi*(1+cos(t))*2
vfit_size = 2*maxy
