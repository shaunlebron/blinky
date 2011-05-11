angleScale = 0.5

function r_to_theta(r)
   return atan(r)/angleScale
end

function theta_to_r(theta)
   return tan(theta*angleScale)
end
