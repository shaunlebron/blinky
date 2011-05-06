map = "r_to_theta"

function r_to_theta(r)
   -- r = f*theta
   if r > pi then
      return nil
   end
   return r
end

function theta_to_r(theta)
   return theta
end

function init(fov,width,height,frame)
   return theta_to_r(fov*0.5) / (frame*0.5)
end
