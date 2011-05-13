
hsym = true
vsym = true
max_hfov = 360
max_vfov = 180

function latlon_to_xy(lat,lon)
   if lat == 0 then
      return lon, 0
   end
   local t = asin(abs(2*lat/pi))
   if abs(lat) == pi/2 then
      local y2 = pi*tan(t/2)
      if y2*lat < 0 then
         y2 = -y2
      end
      return 0,y2
   end
   local a = 0.5*abs(pi/lon - lon/pi)
   local g = cos(t)/(sin(t)+cos(t)-1)
   local p = g*(2/sin(t) - 1)
   local q = a*a+g

   local x = pi*(a*(g-p*p) + sqrt(a*a*(g-p*p)*(g-p*p)-(p*p+a*a)*(g*g-p*p)))/(p*p+a*a)
   local y = pi*(p*q-a*sqrt((a*a+1)*(p*p+a*a) - q*q))/(p*p+a*a)

   if lon*x < 0 then
      x = -x
   end
   if lat*y < 0 then
      y = -y
   end
   return x,y
end

local x,y = latlon_to_xy(pi/2,0)
vfit_size = 2*y

x,y = latlon_to_xy(0,pi)
hfit_size = 2*x
