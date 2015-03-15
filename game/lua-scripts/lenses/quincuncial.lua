eps = 0.0001
halfpi = pi/2

--------------------------------------------------------------------
-- Matlab's Jacobi Elliptic function:
--    returns [sn, cn, dn, ph](u|m).
-- implemntation from d3-geo-projection:
--   (at https://github.com/d3/d3-geo-projection/blob/26b0147156534b3e09f402a2628d0fe209d33f8b/src/elliptic.js#L26-L74)
function asqrt(x)
  if x > 0 then
    return sqrt(x)
  end
  return 0
end
function ellipj(u, m)
  local ai, b, phi, t, twon
  if (m < eps) then
    t = sin(u)
    b = cos(u)
    ai = .25 * m * (u - t * b)
    return t - ai * b,
      b + ai * t,
      1 - .5 * m * t * t,
      u - ai
  end
  if (m >= 1 - eps) then
    ai = .25 * (1 - m)
    b = cosh(u)
    t = tanh(u)
    phi = 1 / b
    twon = b * sinh(u)
    return t + ai * (twon - u) / (b * b),
      phi - ai * t * phi * (twon - u),
      phi + ai * t * phi * (twon + u),
      2 * atan(exp(u)) - halfpi + ai * (twon - u) / b
  end

  local a = {1, 0, 0, 0, 0, 0, 0, 0, 0}
  local c = {sqrt(m), 0, 0, 0, 0, 0, 0, 0, 0}
  local i = 1
  b = sqrt(1 - m)
  twon = 1

  while (abs(c[i] / a[i]) > eps and i < 9) do
    ai = a[i]
    i = i+1
    c[i] = .5 * (ai - b)
    a[i] = .5 * (ai + b)
    b = asqrt(ai * b)
    twon = twon*2
  end

  phi = twon * a[i] * u
  repeat
    b = phi
    t = c[i] * sin(b) / a[i]
    phi = .5 * (asin(t) + phi)
    i = i-1
  until (i == 1)

  t = cos(phi)
  return sin(phi), t, t / cos(phi - b), phi
end

--------------------------------------------------------------------
-- from Appendix A of "Warping Pierce Quincuncial Panoramas"
--    by Chamberlain Fong, Brian Vogel
--    http://arxiv.org/pdf/1011.3189.pdf

sqrt2 = sqrt(2)
sqrt22 = sqrt2/2
m = 1/2
ke = 1.85407467730137

function cnrectify(x,y)
  -- mapping of square coordinates to spherical coordinates
  -- input: x,y in the normalized square with corners at (+-1,+-1)
  -- output: latitude, longitude coordinates to fetch in a sphere
  local xpr = ke*(sqrt22*x-sqrt22*y)/sqrt2+ke
  local ypr = ke*(sqrt22*x+sqrt22*y)/sqrt2
  local sni,cni,dni
  local x1,y1
  local phi,psi
  local s,c,d
  local s1,c1,d1
  local delta
  if abs(ypr)<eps then
    sni,cni,dni = ellipj(xpr,m)
    x1 = cni
    y1 = 0.0
  else
    phi = xpr
    psi = ypr
    s,c,d = ellipj(phi,m)
    s1,c1,d1 = ellipj(psi,1-m)
    delta = c1^2 + m*s^2*s1^2
    x1=(c*c1)/delta
    y1=-(s*d*s1*d1)/delta
  end
  -- stereographic projection equations
  longd = atan2(y1,x1)
  latp = 2*atan2(sqrt(x1*x1+y1*y1),1)-halfpi
  return latp, longd
end

lens_height = 2*sqrt2
lens_width = 2*sqrt2
onload = "f_contain"

--[[----------------------------------------------------------------

Inverse mapping goes through two coordinate frames:

 Quincuncial coord frame:

           /-----------\ y=sqrt2
           |C  _/^\_  B|
           | _/     \_ |
           |/         \|
           |\_   ^   _/|
           |  \_ | _/  |
           |D   \|/   A|
           \-----------/ y=-sqrt2
        x=-sqrt2     x=sqrt2

 Intermediate coord frame:

         FRONT        BACK
    /-------------------------\ y=1
    |            |            |
    |            |            |
    |            |            |
    |         _  |  _         |
    |     UP |\_ | _/| UP     |
    |           \|/           |
    \-------------------------/ y=-1
    x=-2                    x=2

    /-------------------------\ y=1
    |            | \_  B   _/ |
    |            |   \_  _/   |
    |            |A    \/    C|
    |            |    _/\_    |
    |            |  _/    \_  |
    |            |_/   D    \_|
    \-------------------------/ y=-1
    x=-2                    x=2

--]]
function rotate(a,b,angle)
  local c = cos(angle)
  local s = sin(angle)
  local a0 = a*c - b*s
  local b0 = a*s + b*c
  return a0, b0
end

function lens_inverse_intermediate(x,y)
  if abs(x) > 2 or abs(y) > 1 then
    return nil
  end
  x = x+1
  local lat,lon = cnrectify(x,y)
  local x0,y0,z0 = latlon_to_ray(lat,-lon)

  -- rotate from south pole to origin
  local x1,y1,z1 = x0, z0, -y0
  return x1,y1,z1
end

function lens_inverse(x,y)
  -- outside boundary
  if abs(x) > sqrt2 or abs(y) > sqrt2 then
    return nil
  end

  local x0,y0

  -- front
  if abs(x)+abs(y) <= sqrt2 then
    x0, y0 = rotate(x,y,pi/4)
    x0 = x0-1

  -- lower right
  elseif x>0 and y<0 then
    x0, y0 = rotate(x,y,pi/4)
    x0 = x0-1

  -- upper left
  elseif x<0 and y>0 then
    x0, y0 = rotate(x,y,pi/4)
    x0 = x0+3

  -- lower left
  elseif x<0 and y<0 then
    x0, y0 = rotate(x,y,pi/4+pi)
    x0, y0 = x0+1, y0-2

  -- upper right
  else
    x0, y0 = rotate(x,y,pi/4+pi)
    x0, y0 = x0+1, y0+2
  end

  return lens_inverse_intermediate(x0,y0)
end

