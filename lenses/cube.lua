cols = 4
rows = 3

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

function xy_to_cubemap(x,y)
   local r,v = row(y)
   local c,u = col(x)
   if r == 0 then
      if c == 1 then
         return TOP,u,v
      end
      return TOP,1-u,1-v
   end
   if r == 2 then
      if c == 1 then
         return BOTTOM,u,v
      end
      return BOTTOM,1-u,1-v
   end
   if c == 0 then
      return LEFT,u,v
   end
   if c == 1 then
      return FRONT,u,v
   end
   if c == 2 then
      return RIGHT,u,v
   end
   if c == 3 then
      return BEHIND,u,v
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
