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

function init(fov, width, height, frame)
   local t = solveTheta(pi*0.5)
   maxy = 2*sqrt(pi/(4+pi))*sin(t)

   if frame == width then
      t = solveTheta(0)
      local w = 2/sqrt(pi*(4+pi))*(fov*0.5)*(1+cos(t))
      return w / (frame*0.5)
   elseif frame == height then
      t = solveTheta(fov*0.5)
      local h = 2*sqrt(pi/(4+pi))*sin(t)
      return h / (frame*0.5)
   else
      return nil
   end
end
