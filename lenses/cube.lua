cols = 4
rows = 3

hfit_size = cols

-- ignore the top half of the top row
--   and the bottom half of the bottom row
vfit_size = rows-1

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
   local r = row(y)
   local c = col(x)
   if r < 0 or r >= rows or c < 0 or c >= cols then
      return nil
   end
   if r == 0 or r == 2 then
      if not (c == 1 or c == 3) then
         return nil
      end
   end

   local front = 0 
   local right = 1
   local left = 2
   local back = 3
   local top = 4
   local bottom = 5

   local plate
   local r,v = row(y)
   local c,u = col(x)
   if r == 0 then
      if c == 1 then
         plate = top
      else
         plate,u,v = top,1-u,1-v
      end
   elseif r == 2 then
      if c == 1 then
         plate = bottom
      else
         plate,u,v = bottom,1-u,1-v
      end
   elseif c == 0 then
      plate = left
   elseif c == 1 then
      plate = front
   elseif c == 2 then
      plate = right
   elseif c == 3 then
      plate = back
   else
      return nil
   end

   return plate_to_ray(plate,u,v)
end
