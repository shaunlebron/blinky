-- cos of the standard parallel at 50 degrees 28'
clat0 = 2/pi

max_fov = 360
max_vfov = 180

onload = "f_contain"

function lens_forward(x,y,z)
   local lat,lon = ray_to_latlon(x,y,z)
   local clat = cos(lat)
   local temp = clat*cos(lon*0.5)
   local D = acos(temp)
   local C = 1 - temp*temp
   temp = D/sqrt(C)

   local x = 0.5 * (2*temp*clat*sin(lon*0.5)+lon*clat0)
   local y = 0.5 * (temp*sin(lat) + lat)

   return x,y
end

-- from:
-- https://github.com/d3/d3-geo-projection/blob/master/src/winkel3.js
function lens_inverse(x,y)

  if abs(y) >= lens_height/2 then
    return nil
  end

  local lambda = x
  local phi = y
  eps = 0.0001
  halfpi = pi/2

  for iter=1,25 do
    local cosphi = cos(phi)
    local sinphi = sin(phi)
    local sin_2phi = sin(2 * phi)
    local sin2phi = sinphi * sinphi
    local cos2phi = cosphi * cosphi
    local sinlambda = sin(lambda)
    local coslambda_2 = cos(lambda / 2)
    local sinlambda_2 = sin(lambda / 2)
    local sin2lambda_2 = sinlambda_2 * sinlambda_2
    local C = 1 - cos2phi * coslambda_2 * coslambda_2
    local E
    local F
    if C ~= 0 then
      F = 1/C
      E = acos(cosphi * coslambda_2) * sqrt(F)
    else
      E = 0
      F = 0
    end
    local fx = .5 * (2 * E * cosphi * sinlambda_2 + lambda / halfpi) - x
    local fy = .5 * (E * sinphi + phi) - y
    local sigxsiglambda = .5 * F * (cos2phi * sin2lambda_2 + E * cosphi * coslambda_2 * sin2phi) + .5 / halfpi
    local sigxsigphi = F * (sinlambda * sin_2phi / 4 - E * sinphi * sinlambda_2)
    local sigysiglambda = .125 * F * (sin_2phi * sinlambda_2 - E * sinphi * cos2phi * sinlambda)
    local sigysigphi = .5 * F * (sin2phi * coslambda_2 + E * sin2lambda_2 * cosphi) + .5
    local denominator = sigxsigphi * sigysiglambda - sigysigphi * sigxsiglambda
    local siglambda = (fy * sigxsigphi - fx * sigysigphi) / denominator
    local sigphi = (fx * sigysiglambda - fy * sigxsiglambda) / denominator
    lambda = lambda - siglambda
    phi = phi - sigphi
    if abs(siglambda) < eps and abs(sigphi) < eps then
      break
    end
  end

  lat,lon = phi, lambda
  x0,y0 = lens_forward(latlon_to_ray(lat, pi))
  if abs(x) < abs(x0) then
    return latlon_to_ray(lat, lon)
  end
  return nil
end

local x,y = lens_forward(latlon_to_ray(pi/2,0))
lens_height = 2*y

x,y = lens_forward(latlon_to_ray(0,pi))
lens_width = 2*x
