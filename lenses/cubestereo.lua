
hsym = true
vsym = true
max_hfov = 270
max_vfov = 270

function projectcube(x,y,z)
   local magx = abs(x)
   local magy = abs(y)
   local magz = abs(z)
   local mag = magz

   if magx >= magy and magx >= magz then
      mag = magx
   elseif magy >= magx and magy >= magz then
      mag = magy
   end

   return x / mag, y / mag, z / mag
end

function lens_forward(rx,ry,rz)
   x,y,z = projectcube(rx,ry,rz)
   return x/(z+1)*2, y/(z+1)*2
end

function lens_inverse(x,y)
   local rx,ry,rz

   local magx = abs(x)
   local magy = abs(y)
   local z = 2

   if magx <= 1 and magy <= 1 then
      rx = x
      ry = y
      rz = z-1
   elseif magx > magy then
      rx = x / magx
      ry = y / magx
      rz = z / magx-1
   else
      rx = x / magy
      ry = y / magy
      rz = z / magy-1
   end

   local len = sqrt(rx*rx+ry*ry+rz*rz)
   return rx/len, ry/len, rz/len
end
