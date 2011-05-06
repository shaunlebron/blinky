map = "latlon_to_xy"
maxFovHeight = pi

-- cos of the standard parallel at 50 degrees 28'
clat0 = 2/pi

function latlon_to_xy(lat,lon)
   local clat = cos(lat)
   local temp = clat*cos(lon*0.5)
   local D = acos(temp)
   local C = 1 - temp*temp
   temp = D/sqrt(C)

   local x = 0.5 * (2*temp*clat*sin(lon*0.5)+lon*clat0)
   local y = 0.5 * (temp*sin(lat) + lat)

   return x,y
end

function init(fov,width,height,frame)
   local w,h
   if frame == width then
      w,h = latlon_to_xy(0,fov*0.5)
      return w / (frame*0.5)
   elseif frame == height then
      w,h = latlon_to_xy(fov*0.5,0)
      return h / (frame*0.5)
   else
      return nil
   end
end
