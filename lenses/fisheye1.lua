map = "r_to_theta"

function r_to_theta(r)
   -- r = f*theta
   if r > math.pi then
      return nil
   end
   return r
end

function init(fov,width,height,frame)
   return fov / frame;
end
