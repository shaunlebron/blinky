cols = 3
rows = 2

hfit_size = cols
vfit_size = rows

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
   local r,v = row(y)
   local c,u = col(x)
   local plate = r*cols+c

   return plate_to_ray(plate,u,v)
end
