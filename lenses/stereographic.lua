angleScale = 0.5

hsym = true
vsym = true
max_hfov = 360
max_vfov = 360

function r_to_theta(r)
   return atan(r)/angleScale
end

function theta_to_r(theta)
   return tan(theta*angleScale)
end
