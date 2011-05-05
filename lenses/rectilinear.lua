map = "r_to_theta"
maxFovWidth = math.pi
maxFovHeight = math.pi

function r_to_theta(r)
   -- r = f*tan(theta)
   return math.atan(r)
end

function init(fov,width,height,frame)
   return math.tan(fov*0.5) / (frame*0.5)
end
