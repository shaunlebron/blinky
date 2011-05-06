map = "xy_to_ray"
maxFovHeight = pi

function xy_to_ray(x,y)
   local t = 4/(x*x+4)
   return x*t,y*t,2*t-1
end

function theta_to_r(theta)
   local D = 1
   return sin(theta)*(D+1)/(D+cos(theta))
end

function init(fov,width,height,frame)
   return theta_to_r(fov*0.5) / (frame*0.5)
   --return 2*tan(fov*0.25) / (frame*0.5)
end
