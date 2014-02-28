cols = 4
rows = 3

hfit_size = cols
vfit_size = rows
max_hfov = 360
max_vfov = 180

function col(x)
   local nx = x+cols/2
   local i,f = math.modf(nx)
   if nx < 0 then
      return i-1, f+1
   end
   return i,f
end

function row(y)
   local ny = -y+rows/2
   local i,f = math.modf(ny)
   if ny < 0 then
      return i-1, f+1
   end
   return i,f
end

function lens_inverse(x,y)
   x = x - 0.5
   local r,v = row(y)
   local c,u = col(x)
   u = u - 0.5
   v = v - 0.5
   v = -v

   if r < 0 or r >= rows or c < -1 or c >= cols then
      return nil
   end
   if r == 0 or r == 2 then
      if not (c == 1) then
         return nil
      end
   end

   local plate
   if r == 0 then 
      -- top
      return u,0.5,-v
   elseif r == 2 then
      -- bottom
      return u,-0.5,v
   elseif c == 0 then
      -- left
      return -0.5,v,u
   elseif c == 1 then
      -- front
      return u,v,0.5
   elseif c == 2 then
      -- right
      return 0.5,v,-u
   elseif c == 3 or c == -1 then
      -- back
      return -u,v,-0.5
   else
      return nil
   end
end

function lens_forward(x,y,z)
   -- only to be used for FOV

   local ax = abs(x)
   local ay = abs(y)
   local az = abs(z)

   local max = math.max(ax,ay,az)

   local u,v
   if max == ax then
      if x > 0 then
         -- right
         u = -z/x*0.5
         v = y/x*0.5
         return 1+u,v
      else
         -- left
         u = z/-x*0.5
         v = y/-x*0.5
         return -1+u,v
      end
   elseif max == ay then
      if y > 0 then
         -- top
         u = x/y*0.5
         v = -z/y*0.5
         return u,1+v
      else
         -- bottom
         u = x/-y*0.5
         v = z/-y*0.5
         return u,-1+v
      end
   elseif max == az then
      if z > 0 then
         -- front
         u = x/z*0.5
         v = y/z*0.5
         return u,v
      else
         -- back
         u = -x/-z*0.5
         v = y/-z*0.5
         if u > 0 then
            return -2+u,v
         else
            return 2+u,v
         end
      end
   end

end
