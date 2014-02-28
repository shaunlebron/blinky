angleScale = 0.5

hsym = true
vsym = true
max_hfov = 360
max_vfov = 360

function lens_inverse(x,y)
   local r = sqrt(x*x+y*y)
   local theta = atan(r)/angleScale

   local s = sin(theta)
   return x/r*s, y/r*s, cos(theta)
end

function lens_forward(x,y,z)
   local theta = acos(z)

   local r = tan(theta*angleScale)

   local c = r/sqrt(x*x+y*y)
   return x*c, y*c
end
