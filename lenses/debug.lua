cols = numplates
rows = 1

lens_width = cols
lens_height = rows

onload = "f_fit"

function col(x)
   local nx = x+cols/2
   local i,f = math.modf(nx)
   if nx < 0 or nx >= cols then
      return nil, nil
   end
   return i,f
end

function row(y)
   local ny = -y+rows/2
   local i,f = math.modf(ny)
   if ny < 0 or ny >= rows then
      return nil, nil
   end
   return i,f
end

function lens_inverse(x,y)
   local r,v = row(y)
   local c,u = col(x)
   if r == nil or c == nil then
      return nil 
   else
      local plate = r*cols+c
      return plate_to_ray(plate,u,v)
   end
end
