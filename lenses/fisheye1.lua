hsym = true
vsym = true
max_hfov = 360
max_vfov = 360
hfit_size = 2*pi
vfit_size = 2*pi

function lens_inverse(x,y)
   local r = sqrt(x*x+y*y)

   if r > pi then
      return nil
   end
   local theta = r

   local s = sin(theta)
   return x/r*s, y/r*s, cos(theta)
end

function lens_forward(x,y,z)
   local theta = acos(z)

   local r = theta

   local c = r/sqrt(x*x+y*y)
   return x*c, y*c
end
