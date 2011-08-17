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

function lens_inverse(x,y)
   local r,v = row(y)
   local c,u = col(x)
   if r < 0 or r >= rows or c < 0 or c >= cols then
      return nil
   end
   if r == 0 or r == 2 then
      if not (c == 1) then
         return nil
      end
   end

   local front = 0 
   local right = 1
   local left = 2
   local back = 3
   local top = 4
   local bottom = 5
   local rx,ry,rz

   local plate
   if r == 0 then
      plate = top
   elseif r == 2 then
      plate = bottom
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

   u = u - 0.5
   v = v - 0.5
   v = -v

   if plate == front then
      rx = u
      ry = v
      rz = 0.5
   elseif plate == right then
      rx = 0.5
      ry = v
      rz = -u
   elseif plate == left then
      rx = -0.5
      ry = v
      rz = u
   elseif plate == back then
      rx = -u
      ry = v
      rz = -0.5
   elseif plate == top then
      rx = u
      ry = 0.5
      rz = -v
   elseif plate == bottom then
      rx = u
      ry = -0.5
      rz = v
   end

   return rx,ry,rz
end
