maxr = 2*sin(pi*0.5)

hsym = true
vsym = true
max_hfov = 360
max_vfov = 360
hfit_size = maxr*2
vfit_size = maxr*2

function r_to_theta(r)
   return 2*asin(r*0.5)
end

function theta_to_r(theta)
   return 2*sin(theta*0.5)
end

function r_isvalid(r)
   return r <= maxr
end
