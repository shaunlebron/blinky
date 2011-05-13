
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

function xy_isvalid(x,y)
   return x*x+y*y<=maxr*maxr
end

TOL		= 1.e-10
THIRD		= .33333333333333333333
TWO_THRD	= .66666666666666666666
C2_27		= .07407407407407407407
PI4_3		= 4.18879020478639098458
PISQ		= 9.86960440108935861869
TPISQ		= 19.73920880217871723738
HPISQ		= 4.93480220054467930934

function xy_to_latlon(x,y)
   local lat,lon
   local t, c0, c1, c2, c3, al, r2, r, m, d, ay, x2, y2

   x2 = x*x
   ay = abs(y)
   if ay < TOL then
      lat = 0
      t = x2*x2 + TPISQ * (x2 + HPISQ)
      if abs(x) <= TOL then
         lon = 0
      else
         lon = 0.5 * (x2 - PISQ + sqrt(t)) / x
      end
      return lat,lon
   end

   y2 = y*y
   r = x2+y2
   r2 = r*r
   c1 = -pi*ay*(r+PISQ)
   c3 = r2 + (2*pi)*(ay*r+pi*(y2+pi*(ay+pi/2)))
   c2 = c1 + PISQ * (r-3*y2)
   c0 = pi*ay
   c2 = c2/c3
   al = c1 / c3 - THIRD * c2*c2
   m = 2 *sqrt(-THIRD*al)
   d = C2_27*c2*c2*c2+(c0*c0-THIRD*c2*c1)/c3
   d = 3*d/(al*m)
   t = abs(d)
   if (t - TOL <= 1) then
      if t > 1 then
         if d > 0 then
            d = 0
         else
            d = pi
         end
      else
         d = acos(d)
      end
      lat = pi * (m*cos(d*THIRD+PI4_3) - THIRD*c2)
      if y < 0 then
         lat = -lat
      end
      t = r2 + TPISQ * (x2-y2+HPISQ)
      if abs(x) <= TOL then
         lon = 0
      else
         if t <= 0 then
            lon = 0.5 * (r - PISQ) / x
         else
            lon = 0.5 * (r - PISQ + sqrt(t)) / x
         end
      end
   else
      return nil
   end
   return lat,lon
end

maxr = latlon_to_xy(0,pi)
vfit_size = 2*maxr
hfit_size = 2*maxr
