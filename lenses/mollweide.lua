map = "xy_to_latlon"
maxFovHeight = pi

root2 = sqrt(2)

function xy_to_latlon(x,y)
   if x*x/8 + y*y/2 > 1 then
      return nil
   end

   local t = asin(y/root2);
   local lon = pi*x/(2*root2*cos(t))
   local lat = asin((2*t+sin(2*t))/pi)
   return lat,lon
end

function solveTheta(lat)
   local t = lat
   local dt = 1000
   repeat
      dt = -(t + sin(t) - pi*sin(lat))/(1+cos(t))
      t = t+dt
   until dt < 0.001
   return t/2
end

function init(fov,width,height,frame)
   if frame == width then
      local t = solveTheta(0)
      local w = 2*root2/pi*(fov*0.5)*cos(t)
      return w / (frame*0.5)
   elseif frame == height then
      local t = solveTheta(fov*0.5)
      local h = root2*sin(t)
      return h / (frame*0.5)
   else
      return nil
   end
end
