map = "r_to_theta"

function r_to_theta(r)
   -- r = 2*f*sin(theta/2)

   local maxr = 2 * sin(pi/2)
   if r > maxr then
      return nil
   end

   return 2*asin(r*0.5)
end

function theta_to_r(theta)
   return 2*sin(theta*0.5)
end

function init(fov,width,height,frame)
   return theta_to_r(fov*0.5) / (frame*0.5);
end
