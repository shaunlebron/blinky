map = 0

function inverse(x,y)
   -- r = f*theta

   local r = math.sqrt(x*x+y*y)
   local el = r;

   if el > math.pi then
      return 0
   end

   local rr = 1/r
   local s = math.sin(el)
   local c = math.cos(el)
   
   return {x*rr*s, y*rr*s, c}
end

function init(fov,width,height,frame)

   if fov > 2*math.pi then 
      return 0
   end

   scale = fov / frame;
   return 1
end
