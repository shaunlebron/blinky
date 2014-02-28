maxr = 2*sin(pi*0.5)

hsym = true
vsym = true
max_hfov = 360
max_vfov = 360
hfit_size = maxr*2
vfit_size = maxr*2

function lens_inverse(x,y)
   local r = sqrt(x*x+y*y)
   if r > maxr then
      return nil
   end

   local theta = 2*asin(r*0.5)

   local s = sin(theta)
   return x/r*s, y/r*s, cos(theta)
end

function lens_forward(x,y,z)
   local theta = acos(z)

   local r = 2*sin(theta*0.5)

   local c = r/sqrt(x*x+y*y)
   return x*c, y*c
end
