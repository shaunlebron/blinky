if numplates == 4 then
    rows = 2
    cols = {2,2}
elseif numplates == 5 then
    rows = 2
    cols = {3,2}
elseif numplates == 6 then
    rows = 2
    cols = {3,3}
else
    rows = 1
    cols = {numplates}
end
maxcols = math.max(table.unpack(cols))

lens_width = maxcols
lens_height = rows

onload = "f_contain"

function col(rowcols,x)
   local nx = x+rowcols/2
   local i,f = math.modf(nx)
   if nx < 0 or nx >= rowcols then
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
   if r == nil then
      return nil 
   end

   local c,u = col(cols[r+1],x)
   if c == nil then
      return nil
   else
      local plate = c
      local i = 0
      while i < r do
         plate = plate + cols[i+1]
         i = i + 1
      end
      return plate_to_ray(plate,u,v)
   end
end
