map = 0

function inverse(x,y)
   -- r = f*tan(theta)

   local r = math.sqrt(x*x+y*y)
   local el = math.atan(r)

   local rr = 1/r
   local s = math.sin(el)
   local c = math.cos(el)
   
   return {x*rr*s, y*rr*s, c}
end

function init(fov,width,height,frame)

   if fov > math.pi then 
      return 0
   end

   scale = math.tan(fov*0.5) / (frame*0.5)
   return 1
end
