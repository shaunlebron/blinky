hsym = true
vsym = true
size = 2.622058
hfit_size = 2*size
vfit_size = 2*size

ORDER = 8
RSQRT2 = 0.7071067811865475244008443620

function ell_int_5(phi)
   local d1 = 0
   local d2 = 0
   local y,y2,temp
   local C = { 2.19174570831038, 0.0914203033408211, -0.00575574836830288, -0.0012804644680613, 5.30394739921063e-05, 3.12960480765314e-05, 2.02692115653689e-07, -8.58691003636495e-07}
   local Cp = C[ORDER]
   
   y = phi * 2/pi
   y = 2*y*y-1
   y2 = 2*y

   for i=ORDER-1,1,-1 do
      d1,d2 = y2*d1-d2+Cp, d1
      Cp = C[i]
   end

   return phi*(y*d1-d2+Cp/2)
end

function proj_asin(v)
   local av = abs(v)
   if av >= 1 then
      if v < 0 then
         return -pi/2
      else
         return pi/2
      end
   end
   return asin(v)
end

function proj_acos(v)
   local av = abs(v)
   if av >= 1 then
      if v < 0 then
         return pi
      else
         return 0
      end
   end
   return acos(v)
end

function latlon_to_xy(lat,lon)
   local m,n
   local a = 0
   local b = 0
   local sm = false
   local sn = false
   local back = false

   local sp = sin(lat)
   if (abs(lon) - 0.0000001) > pi then
      return nil
   end
   if (abs(lon) - 0.0000001) > pi/2 then
      back = true
      if lon > 0 then
         lon = lon - pi
      else
         lon = lon + pi
      end
   end

   if (abs(lon) - 0.0000001) > pi/2 then
      return nil
   end

   a = cos(lat) * sin(lon)
   if sp + a < 0 then
      sm = true
   end
   if sp - a < 0 then
      sn = true
   end
   a = proj_acos(a)
   b = pi/2 - lat

   m = proj_asin(sqrt(abs(1+cos(a+b))))
   if sm then
      m = -m
   end
   n = proj_asin(sqrt(abs(1-cos(a-b))))
   if sn then
      n = -n
   end
   local x = ell_int_5(m)
   local y = ell_int_5(n)

   -- rotate by 45 degrees
   local temp = x
   x = RSQRT2 * (x-y)
   y = RSQRT2 * (temp+y)

   if back then
      x = -x
      if x*y > 0 then
         x,y = -y,-x
      else
         x,y = y,x
      end
      if x > 0 then
         x = x - hfit_size/2
      else
         x = x + hfit_size/2
      end
      if y > 0 then
         y = y - vfit_size/2
      else
         y = y + vfit_size/2
      end
   else
   end
   return x,y
end
