map = 0

function inverse(x,y)
   -- r = 2f*tan(theta/2)

   local r = math.sqrt(x*x+y*y)
   local el = 2*math.atan(r,2)

   if el > math.pi then
      return 0
   end

   local rr = 1/r
   local s = math.sin(el)
   local c = math.cos(el)
   
   return {x*rr*s, y*rr*s, c}
end

function init(fov,width,height,frame)

   if fov >= 2*math.pi then 
      return 0
   end

   scale = 2*math.tan(fov*0.25) / (frame*0.5)
   return 1
end
