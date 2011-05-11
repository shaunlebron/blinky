FC = 0.92131773192356127802
RP = 0.31830988618379067154

function latlon_to_xy(lat,lon)
   local x = FC * lon * (1 - RP * abs(lat))
   local y = FC * lat
   return x,y
end
