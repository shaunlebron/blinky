map = 0
gumbyScale = 0.75

function inverse(x,y)

   local t = 4/(x*x+4)
   local rx = x*t
   local ry = y*t
   local rz = 2*t-1
   local lon = math.atan2(rx,rz)
   local lat = math.atan2(ry,math.sqrt(rx*rx+rz*rz))

   lat = lat/gumbyScale
   lon = lon/gumbyScale

   local clat = math.cos(lat)

   return {
      math.sin(lon)*clat,
      math.sin(lat),
      math.cos(lon)*clat}
end

function init(fov,width,height,frame)

   if frame == width then
      if fov > math.pi*2 then
         return 0
      end
   elseif frame == height then
      if fov >= math.pi then
         return 0
      end
   else
      return 0
   end

   local r = (frame*0.5) / math.tan((fov*0.5)/2*gumbyScale)/2
   scale = 1/r
   return 1
end
