map = "r_to_theta"

function r_to_theta(r)
   -- r = 2f*tan(theta/2)

   local el = 2*atan(r,2)
   if el > pi then
      return nil
   end
   return el
end

function theta_to_r(theta)
   return 2*tan(theta*0.5)
end

function init(fov,width,height,frame)
   return theta_to_r(fov*0.5) / (frame*0.5)
end
