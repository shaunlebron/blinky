hsym = true
vsym = true
max_hfov = 180
max_vfov = 180

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

function lens_forward(x,y,z)
   local lat,lon = ray_to_latlon(x,y,z)

   local m,n
   local a = 0
   local b = 0
   local sm = 0
   local sn = 0

   if abs(lon) > pi/2 then
      return nil
   end
   if abs(abs(lat)-pi/2) < 0.0000001 then
      if lat < 0 then
         return 0,-1.85407
      else
         return 0,1.85407
      end
   end

   local sl = sin(lon)
   local sp = sin(lat)
   local cp = cos(lat)
   a = proj_acos((cp*sl-sp)*RSQRT2)
   b = proj_acos((cp*sl+sp)*RSQRT2)
   m = proj_asin(sqrt(abs(1+cos(a+b))))
   if lon < 0 then
      m = -m
   end
   n = proj_asin(sqrt(abs(1-cos(a-b))))
   if lat < 0 then
      n = -n
   end
   local x = ell_int_5(m)
   local y = ell_int_5(n)
   return x,y
end
