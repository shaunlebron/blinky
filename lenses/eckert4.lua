map = "xy_to_latlon"
maxFovHeight = pi
maxx = 0
maxy = 0

function solveTheta(lat)
   local t = lat/2;
   local dt = 0
   repeat
      dt = -(t + sin(t)*cos(t) + 2*sin(t) - (2+pi*0.5)*sin(lat))/(2*cos(t)*(1+cos(t)))
      t = t+dt
   until dt < 0.001
   return t
end

function xy_to_latlon(x,y)
   if abs(y) > maxy then
      return nil
   end
   local t = asin(y/2*sqrt((4+pi)/pi))
   local lat = asin((t+sin(t)*cos(t)+2*sin(t))/(2+pi*0.5))
   if y ~= lasty then
      local t2 = solveTheta(abs(lat))
      maxx = 2/sqrt(pi*(4+pi))*pi*(1+cos(t2))
      lasty = y
   end

   if abs(x) > maxx then
      return nil
   end

   local lon = sqrt(pi*(4+pi))*x/(2*(1+cos(t)))

   return lat,lon
end

function latlon_to_xy(lat,lon)
   local t = solveTheta(lat)
   local x = 2/sqrt(pi*(4+pi))*lon*(1+cos(t))
   local y = 2*sqrt(pi/(4+pi))*sin(t)
   return x,y
end

function init(fov, width, height, frame)
   local t = solveTheta(pi*0.5)
   maxy = 2*sqrt(pi/(4+pi))*sin(t)

   local x,y

   if frame == width then
      x,y = latlon_to_xy(0,fov*0.5)
      return x / (frame*0.5)
   elseif frame == height then
      x,y = latlon_to_xy(fov*0.5,0)
      return y / (frame*0.5)
   else
      return nil
   end
end
