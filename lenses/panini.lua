map = "xy_to_ray"
maxFovHeight = pi

function xy_to_ray(x,y)
   local t = 4/(x*x+4)
   return x*t,y*t,2*t-1
end

function init(fov,width,height,frame)
   return 2*tan(fov*0.25) / (frame*0.5)
end
