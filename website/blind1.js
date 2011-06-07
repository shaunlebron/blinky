window.onload = function() {
   var w = 400,
       h = 200,

       cam = { x:w/2, y:h/2+20, r:5 },
       obj = {x:w/4, y:h/4, r:20},
       screen = { x:w/2, y:h/2, width:w/2},

       bound = function(x,min,max) { return Math.min(Math.max(x,min),max); },

       R = Raphael("figure",w,h),

       camVis = R.circle(cam.x, cam.y, cam.r)
                  .attr({fill:"#000"})

       objVis = R.circle(obj.x, obj.y, obj.r).attr({fill:"#f00",stroke:"none"})
                  .drag(
                        function(dx,dy) {
                           obj.x = bound(this.ox + dx, 0, w),
                           obj.y = bound(this.oy + dy, 0, h);
                           this.attr({cx:obj.x, cy:obj.y});
                           updateCone();
                        },
                        function() {
                           this.ox = this.attrs.cx;
                           this.oy = this.attrs.cy;
                        },
                        function () {
                        }),

       coneVis = R.path().attr({fill:"#f00", opacity:"0.5", stroke:"none"}),

       updateCone = function() {
          var dx = obj.x - cam.x,
              dy = obj.y - cam.y,
              dist = Math.sqrt(dx*dx+dy*dy),
              cx1 = obj.x-obj.r*dy/dist,
              cy1 = obj.y+obj.r*dx/dist,
              cx2 = obj.x+obj.r*dy/dist,
              cy2 = obj.y-obj.r*dx/dist;
          coneVis.attr({path:["M",cam.x,cam.y,"L",cx1,cy1,"L",cx2,cy2,"Z"]});

          var ix1 = (cx1-cam.x)/(cy1-cam.y)*(screen.y-cam.y) + cam.x,
              ix2 = (cx2-cam.x)/(cy2-cam.y)*(screen.y-cam.y) + cam.x;
          imageVis.attr({path:["M",ix1,screen.y,"L",ix2,screen.y]});
       },

       imageVis = R.path().attr({stroke:"#f00"});

       /*
       screenVis = R.path([
                     "M",
                     screen.x - screen.width/2,
                     screen.y,
                     "h", screen.width]);
                     */
   
   updateCone();
   coneVis.insertBefore(objVis);
}
