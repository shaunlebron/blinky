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

function xy_to_plate(x,y)
   local front = 0 
   local right = 1
   local left = 2
   local back = 3
   local top = 4
   local bottom = 5

   local r,v = row(y)
   local c,u = col(x)
   if r == 0 then
      if c == 1 then
         return top,u,v
      end
      return top,1-u,1-v
   end
   if r == 2 then
      if c == 1 then
         return bottom,u,v
      end
      return bottom,1-u,1-v
   end
   if c == 0 then
      return left,u,v
   end
   if c == 1 then
      return front,u,v
   end
   if c == 2 then
      return right,u,v
   end
   if c == 3 then
      return back,u,v
   end

   return nil
end

function xy_isvalid(x,y)
   local r = row(y)
   local c = col(x)
   if r < 0 or r >= rows or c < 0 or c >= cols then
      return false
   end
   if r == 0 or r == 2 then
      return c == 1 or c == 3
   end
   return true
end
