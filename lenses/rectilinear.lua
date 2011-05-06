map = "r_to_theta"
maxFovWidth = pi
maxFovHeight = pi

function r_to_theta(r)
   -- r = f*tan(theta)
   return atan(r)
end

function init(fov,width,height,frame)
   return tan(fov*0.5) / (frame*0.5)
end
