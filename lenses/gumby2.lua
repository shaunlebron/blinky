gumbyScale = 0.75
gumbyScaleInv = 1.0/gumbyScale

function r_to_theta(r)
   return 2*atan2(r,2) * gumbyScaleInv
end

function theta_to_r(theta)
   return 2*tan(theta*0.5*gumbyScale)
end

function r_isvalid(r)
   return 2*atan2(r,2) <= pi
end
