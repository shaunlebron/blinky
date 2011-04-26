map = 0

function inverse(x,y)
   -- r = 2*f*sin(theta/2)

   local r = math.sqrt(x*x+y*y)
   local maxr = 2 * math.sin(math.pi/2)
   if r > maxr then
      return 0
   end

   local el = 2*math.asin(r*0.5)

   local rr = 1/r
   local s = math.sin(el)
   local c = math.cos(el)
   
   return {x*rr*s, y*rr*s, c}
end

function init(fov,width,height,frame)

   if fov > 2*math.pi then 
      return 0
   end

   scale = 2*math.sin(fov*0.25) / (frame*0.5);
   return 1
end
