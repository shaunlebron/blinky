map = "xy_to_latlon"
maxFovHeight = math.pi

function xy_to_latlon(x,y)
   if x*x/8+y*y/2 > 1 then
      return nil
   end

   local z = math.sqrt(1-0.0625*x*x-0.25*y*y)
   local lon = 2*math.atan(z*x/(2*(2*z*z-1)))
   local lat = math.asin(z*y)
   return lat,lon
end

function latlon_to_xy(lat,lon)
end

function init(fov,width,height,frame)
   if frame == width then
      local w = 2*math.sqrt(2)*math.sin(fov*0.25) / math.sqrt(1+math.cos(fov*0.25))
      return w / (frame*0.5)
   elseif frame == height then
      local h = math.sqrt(2)*math.sin(fov*0.5) / math.sqrt(1+math.cos(fov*0.5))
      return h / (frame*0.5)
   else
      return nil
   end
end
