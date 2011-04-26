map = 0

function inverse(x,y)

   local t = 4/(x*x+4)
   return {x*t,y*t,2*t-1}
end

function init(fov,width,height,frame)

   if frame == width then
      if fov > math.pi*2 then
         return 0
      end
      scale = 2*math.tan(fov*0.25) / (frame*0.5)
   elseif frame == height then
      if fov > math.pi then
         return 0
      end
      scale = 2*math.tan(fov*0.25) / (frame*0.5)
   else
      return 0
   end
   return 1
end
