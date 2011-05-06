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

function latlon_to_xy(lat,lon)
   local t = solveTheta(lat)
   local x = 2*sqrt(2)/pi*lon*cos(t)
   local y = sqrt(2)*sin(t)
   return x,y
end

function init(fov,width,height,frame)
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
