map = "r_to_theta"
maxFovWidth = pi
maxFovHeight = pi

function r_to_theta(r)
   -- r = f*tan(theta)
   return atan(r)
end

function theta_to_r(theta)
   return tan(theta)
end

function init(fov,width,height,frame)
   return theta_to_r(fov*0.5) / (frame*0.5)
end
