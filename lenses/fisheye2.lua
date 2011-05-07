function r_to_theta(r)
   return 2*asin(r*0.5)
end

function theta_to_r(theta)
   return 2*sin(theta*0.5)
end

function r_isvalid(r)
   return r <= 2*sin(pi*0.5)
end
