map = "xy_to_latlon"
gumbyScale = 0.75
gumbyScaleInv = 1.0/gumbyScale
maxFovHeight = math.pi

function xy_to_latlon(x,y)

   local t = 4/(x*x+4)
   local rx = x*t
   local ry = y*t
   local rz = 2*t-1
   local lon = math.atan2(rx,rz)
   local lat = math.atan2(ry,math.sqrt(rx*rx+rz*rz))

   lat = lat*gumbyScaleInv
   lon = lon*gumbyScaleInv

   return lat, lon
end

function init(fov,width,height,frame)
   local r = (frame*0.5) / math.tan((fov*0.5)/2*gumbyScale)/2
   return 1/r
end
