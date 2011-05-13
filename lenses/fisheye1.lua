hsym = true
vsym = true
max_hfov = 360
max_vfov = 360
hfit_size = 2*pi
vfit_size = 2*pi

function r_to_theta(r)
   return r
end

function theta_to_r(theta)
   return theta
end

function r_isvalid(r)
   return r <= pi
end
