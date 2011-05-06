map = "r_to_theta"
gumbyScale = 0.75
gumbyScaleInv = 1.0/gumbyScale

function r_to_theta(r)
   local el = 2*atan2(r,2)
   if el > pi then
      return nil
   end
   return el * gumbyScaleInv
end

function theta_to_r(theta)
   return 2*tan(theta*0.5*gumbyScale)
end

function init(fov,width,height,frame)
   return theta_to_r(fov*0.5) / (frame*0.5);
end
