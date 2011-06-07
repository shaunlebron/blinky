window.onload = function() {
   var w = 400,
       h = 200,

       cam = { x:w/2, y:h/2+40, r:5 },
       obj = {x:w/4, y:h/4, r:20},
       screen = { x:w/2, y:h/2, width:w/2},

       bound = function(x,min,max) { return Math.min(Math.max(x,min),max); },
       sign = function(x) { return x < 0 ? -1 : 1; },

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

          //            * (obj.x,obj.y)
          //            |\
          //      cam.r | \ dist
          //            |  \
          //            |___\
          //  (cx1,cy1) *    * (cam.x,cam.y)

          // calculate object's visual cone
          var dx = obj.x - cam.x,
              dy = obj.y - cam.y,
              dist = Math.sqrt(dx*dx+dy*dy),
              ry = obj.r/dist, // cone point on the unit circle
              rx = Math.sqrt(1-ry*ry),
              exx = obj.r*dy/dist, // x-axis vector
              exy = -obj.r*dx/dist,
              eyx = -obj.r*dx/dist,// y-axis vector
              eyy = -obj.r*dy/dist,
              cx1 = obj.x + exx*rx + eyx*ry, // absolute cone end points
              cy1 = obj.y + exy*rx + eyy*ry,
              cx2 = obj.x - exx*rx + eyx*ry,
              cy2 = obj.y - exy*rx + eyy*ry;

          // update object's visual cone
          coneVis.attr({path:["M",cam.x,cam.y,"L",cx1,cy1,"L",cx2,cy2,"Z"]});

          // calculate object's 1D screen image
          var ix1 = (cx1-cam.x)/(cy1-cam.y)*(screen.y-cam.y) + cam.x,
              ix2 = (cx2-cam.x)/(cy2-cam.y)*(screen.y-cam.y) + cam.x;
          if (cy1 >= cam.y) { ix1 = sign(cx1-cam.x) * Infinity; }
          if (cy2 >= cam.y) { ix2 = sign(cx2-cam.x) * Infinity; }
          ix1 = bound(ix1,screen.x - screen.width/2,screen.x+screen.width/2);
          ix2 = bound(ix2,screen.x - screen.width/2,screen.x+screen.width/2);

          if (obj.y-obj.r > cam.y) {
             imageVis.hide();
          }
          else {
             imageVis.show();
          }

          // update object's 1D screen image
          imageVis.attr({path:["M",ix1,screen.y,"H",ix2]});

       },

       imageVis = R.path().attr({"stroke-width":"4px", stroke:"#f00"});

       screenVis = R.path([ "M", screen.x - screen.width/2, screen.y, "h", screen.width]).
          attr({opacity:"0.5"});
   
   updateCone();
   coneVis.insertBefore(objVis);
}
