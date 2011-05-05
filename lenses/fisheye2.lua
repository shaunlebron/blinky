map = "r_to_theta"

function r_to_theta(r)
   -- r = 2*f*sin(theta/2)

   local maxr = 2 * math.sin(math.pi/2)
   if r > maxr then
      return nil
   end

   return 2*math.asin(r*0.5)
end

function init(fov,width,height,frame)
   return 2*math.sin(fov*0.25) / (frame*0.5);
end
