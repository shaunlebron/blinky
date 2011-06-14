window.onload = function() {

   // size of usable area
   var w = 650;
   var h = 400;

   // RaphaelJS object
   var R = Raphael("figure",w,h);

   // camera
   var cam = { x:w/2, y:h/2+40, r:5 };
   var camVis = R.circle(cam.x, cam.y, cam.r)
      .attr({fill:"#000"});
   var camText = R.text(cam.x + cam.r + 30, cam.y, "camera")
      .attr({"font-size":"15px"});

   // 1D screen
   var screen = { x:w/2, y:h/2, width:w};
   var screenVis = R.path([ "M", screen.x - screen.width/2, screen.y, "h", screen.width])
      .attr({opacity:"0.5"});
   var screenText = R.text(screen.x + screen.width/2 + 30, screen.y, "screen")
      .attr({"font-size":"15px"});

   // math utilities
   var bound = function(x,min,max) { return Math.min(Math.max(x,min),max); };
   var sign = function(x) { return x < 0 ? -1 : 1; };

   // class to represent a colored ball 
   var ObjType = function(x,y,radius,color) 
   {
      // obj represents the current instance of this ObjType
      var obj = this;

      // create the position and radius
      obj.x = x;
      obj.y = y;
      obj.r = radius;

      // create the visual circle 
      obj.circle = R.circle(x,y,radius)
         .attr({fill:color,stroke:"none",cursor:"move"});

      // create the touch circle
      obj.touch = R.circle(x,y,radius)
         .attr({fill:"#000",stroke:"none",opacity:"0",cursor:"move"})
         .mouseover(
               function (e) {
               })
         .mouseout(
               function (e) {
               })
         .drag(
               // the "this" pointer refers to the circle object
               // "this.ox" and "this.oy" represent the original location before dragging

               // onmove
               function(dx,dy) {
                  // get mouse position
                  var newx = bound(this.ox + dx, 0, w);
                  var newy = bound(this.oy + dy, 0, h);

                  if (obj.update(newx,newy) == false) {
                     // circle was too close to camera
                     //  so push it away
                     var dx = newx-cam.x;
                     var dy = newy-cam.y;
                     var dist = Math.sqrt(dx*dx+dy*dy);
                     var r = cam.r+obj.r+0.1;
                     newx = cam.x+dx/dist*r;
                     newy = cam.y+dy/dist*r;
                     obj.update(newx,newy);
                  }

                  // update final position
                  obj.circle.attr({cx:newx, cy:newy});
                  obj.touch.attr({cx:newx, cy:newy});
                  obj.x = newx;
                  obj.y = newy;
               },

               // onstart
               function() {
                  this.ox = this.attrs.cx;
                  this.oy = this.attrs.cy;

                  // bring to the front (but behind the objects)
                  // (screenText is used as a middle marker)
                  obj.image.insertBefore(screenText);
                  obj.cone.insertBefore(screenText);
                  obj.circle.insertBefore(screenText);
               },

               // onend
               function () {
                  // useful for creating initial position
                  // alert([obj.x, obj.y]);
                  //obj.cone.animate({opacity:"0"},500,">");
               });

      // create the visual cone
      obj.cone = R.path()
         .attr({fill:color, opacity:"0.2", stroke:"none"});

      // create the visual 1D image
      obj.image = R.path()
         .attr({"stroke-width":"5px", stroke:color});

      // updates the position of the cone and the image depending on the circle
      obj.update = function(newx,newy) {

          // distances between camera and object
          var dx = newx - cam.x;
          var dy = newy - cam.y;
          var dist = Math.sqrt(dx*dx+dy*dy);

          // too close to camera (exit)
          if (dist <= obj.r+cam.r) {
             return false;
          }

          // set cone position
          var angle = Math.atan2(dy,dx);
          var da = Math.asin(obj.r / dist);
          var t = w*h; // arbitrarily large (relies on clipping)
          var cx1 = cam.x + t*Math.cos(angle-da);
          var cy1 = cam.y + t*Math.sin(angle-da);
          var cx2 = cam.x + t*Math.cos(angle+da);
          var cy2 = cam.y + t*Math.sin(angle+da);

          // calculate object's 1D screen image
          var ix1 = (cx1-cam.x)/(cy1-cam.y)*(screen.y-cam.y) + cam.x;
          var ix2 = (cx2-cam.x)/(cy2-cam.y)*(screen.y-cam.y) + cam.x;

          if (cy1 >= cam.y && cy2 >= cam.y) {
             // object is behind camera
             obj.image.attr({path:""});
          }
          else {

             // determine infinite distance cases
             if (cy1 >= cam.y) { 
                ix1 = -Infinity;
             }
             else if (cy2 >= cam.y) { 
                ix2 = Infinity;
             }

             // bound to screen
             ix1 = bound(ix1,screen.x - screen.width/2,screen.x+screen.width/2);
             ix2 = bound(ix2,screen.x - screen.width/2,screen.x+screen.width/2); 

             // update screen image
             obj.image.attr({path:["M",ix1,screen.y,"H",ix2]});
          }
          // update object's visual cone
          obj.cone.attr({path:["M",cam.x,cam.y,"L",cx1,cy1,"L",cx2,cy2,"Z"]});
          return true;
      };

      // insert all right above the 1D screen
      obj.image.insertAfter(screenVis);
      obj.cone.insertAfter(obj.image);
      obj.circle.insertAfter(obj.cone);
      obj.touch.toFront();

      // initialize cone and image positions
      obj.update(obj.x, obj.y);
   };

   var blue = "#5D8AA8";
   var red = "#E32636";
   var green = "#556B2F";

   // create the colored balls
   var makeObj = function() {
      var color = "hsl(" + Math.round(Math.random()*360) + ",60,50)";
      var cone = Math.PI/6*5;
      var angle = Math.random()*cone + (Math.PI-cone)/2;
      var radius = Math.random()*h/4+h/4;
      return new ObjType(
            Math.cos(angle)*radius+cam.x,
            cam.y - Math.sin(angle)*radius,
            20,
            color);
   };

   var obj_count = 3;
   for (var i=0; i<obj_count; ++i) {
      makeObj();
   }
}
