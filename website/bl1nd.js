window.onload = function() {

   // size of usable area
   var w = 650;
   var h = 300;

   // RaphaelJS object
   var R = Raphael("figure",w,h);

   // RaphaelJS icons
   var camIcon = 
      "M24.25,10.25H20.5v-1.5h-9.375v1.5h-3.75c-1.104,0-2,0.896-2,2v10.375c0,1.104,0.896,2,2,2H24.25c1.104,0,2-0.896,2-2V12.25C26.25,11.146,25.354,10.25,24.25,10.25zM15.812,23.499c-3.342,0-6.06-2.719-6.06-6.061c0-3.342,2.718-6.062,6.06-6.062s6.062,2.72,6.062,6.062C21.874,20.78,19.153,23.499,15.812,23.499zM15.812,13.375c-2.244,0-4.062,1.819-4.062,4.062c0,2.244,1.819,4.062,4.062,4.062c2.244,0,4.062-1.818,4.062-4.062C19.875,15.194,18.057,13.375,15.812,13.375z";

   // camera
   var cam = { x:w/2, y:h/2+40, r:5 };
   var camVis = R.path(camIcon).attr({fill:"#000",opacity:"0.8"});
   camVis.translate(cam.x-16,cam.y-10);

   // circle screen
   var circScreen = {r:50 };
   var circScreenVis = R.circle(cam.x, cam.y, circScreen.r)
      .attr({fill:"none",opacity:"0.0"});


   // rectilinear screen
   var rectScreen = { x:w/2, y:h/2-20, width:w*0.8};
   var rectScreenVis = R.path([ 
         "M", rectScreen.x - rectScreen.width/2, rectScreen.y, 
         "h", rectScreen.width])
      .attr({opacity:"0.5"});

   // above screen marker
   // insertBefore(aboveScreen) used to position to the top of the screen
   var aboveScreen = R.path();

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
      obj.circle = R.circle(x,y,radius).attr({fill:color,stroke:"none",cursor:"move"});

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
                  // get object position
                  obj.x = bound(this.ox + dx, 0, w);
                  obj.y = bound(this.oy + dy, 0, h);

                  if (obj.update() == false) {
                     // circle was too close to camera
                     //  so push it away
                     var r = cam.r+obj.r+0.1;
                     obj.x = cam.x+obj.dx/obj.dist*r;
                     obj.y = cam.y+obj.dy/obj.dist*r;
                     obj.update();
                  }

                  // update final position
                  obj.circle.attr({cx:obj.x, cy:obj.y});
                  obj.touch.attr({cx:obj.x, cy:obj.y});
               },

               // onstart
               function() {
                  this.ox = this.attrs.cx;
                  this.oy = this.attrs.cy;

                  // bring in front of the screens
                  obj.rectImage.insertBefore(aboveScreen);
                  obj.circImage.insertBefore(aboveScreen);
                  obj.cone.insertBefore(aboveScreen);
                  obj.circle.insertBefore(aboveScreen);
               },

               // onend
               function () {
                  // useful for creating initial position
                  // alert([obj.x, obj.y]);
                  //obj.cone.animate({opacity:"0"},500,">");
               });

      // create the visual cone
      obj.cone = R.path().attr({fill:color, opacity:"0.2", stroke:"none"});

      // create the rectilinear image
      obj.rectImage = R.path().attr({"stroke-width":"5px", stroke:color});

      // create the circle image
      obj.circImage = R.path().attr({"stroke-width":"5px", stroke:color});

      // update the viewing cone
      obj.updateCone = function() {
         // set cone position
         obj.angle = Math.atan2(obj.dy,obj.dx);
         obj.da = Math.asin(obj.r / obj.dist);
         var t = w*h; // arbitrarily large (relies on clipping)

         // endpoints of the cone
         obj.cx1 = cam.x + t*Math.cos(obj.angle-obj.da);
         obj.cy1 = cam.y + t*Math.sin(obj.angle-obj.da);
         obj.cx2 = cam.x + t*Math.cos(obj.angle+obj.da);
         obj.cy2 = cam.y + t*Math.sin(obj.angle+obj.da);

         // update object's visual cone
         obj.cone.attr({path:[
               "M", cam.x, cam.y,
               "L", obj.cx1, obj.cy1,
               "L", obj.cx2, obj.cy2,
               "Z"]});
      };

      // update the rectilinear projection
      obj.updateRectilinear = function()
      {
          // calculate object's 1D rectScreen image
          var ix1 = (obj.cx1-cam.x) / (obj.cy1-cam.y) * (rectScreen.y-cam.y) + cam.x;
          var ix2 = (obj.cx2-cam.x) / (obj.cy2-cam.y) * (rectScreen.y-cam.y) + cam.x;

          if (obj.cy1 >= cam.y && obj.cy2 >= cam.y) {
             // object is behind camera
             obj.rectImage.attr({path:""});
          }
          else {

             // determine infinite distance cases
             if (obj.cy1 >= cam.y) { 
                ix1 = -Infinity;
             }
             else if (obj.cy2 >= cam.y) { 
                ix2 = Infinity;
             }

             // bound to rectScreen
             ix1 = bound(ix1,rectScreen.x - rectScreen.width/2,rectScreen.x+rectScreen.width/2);
             ix2 = bound(ix2,rectScreen.x - rectScreen.width/2,rectScreen.x+rectScreen.width/2); 

             // update rectScreen image
             obj.rectImage.attr({path:[
                   "M",ix1,rectScreen.y,
                   "H",ix2]});
          }
      };

      // update the circle projection
      obj.updateCircle = function() {

          obj.circImage.attr({path:[
                "M", 
                cam.x + circScreen.r * Math.cos(obj.angle-obj.da), 
                cam.y + circScreen.r * Math.sin(obj.angle-obj.da),
                "A", 
                circScreen.r, circScreen.r, 0, 0, 1,
                cam.x + circScreen.r * Math.cos(obj.angle+obj.da), 
                cam.y + circScreen.r * Math.sin(obj.angle+obj.da)]});
      };

      // updates the position of the cone and the image depending on the circle
      obj.update = function()
      {
         // distances between camera and object
         obj.dx = obj.x - cam.x;
         obj.dy = obj.y - cam.y;
         obj.dist = Math.sqrt(obj.dx*obj.dx+obj.dy*obj.dy);

         // too close to camera (exit)
         if (obj.dist <= obj.r+cam.r) {
            return false;
         }
          
         // update dependent visuals
         obj.updateCone();
         obj.updateRectilinear();
         //obj.updateCircle();

         return true;
      };

      // insert all right above the 1D rectScreen
      obj.rectImage.insertAfter(rectScreenVis);
      obj.circImage.insertAfter(obj.rectImage);
      obj.cone.insertAfter(obj.circImage);
      obj.circle.insertAfter(obj.cone);
      obj.touch.toFront();

      // initialize
      obj.update();
   };

   // function to create a colored ball
   var makeObj = function(hue,angle,radius) {
      var color = "hsl(" + hue + ",60,50)";
      return new ObjType(
            Math.cos(angle)*radius+cam.x,
            cam.y - Math.sin(angle)*radius,
            20,
            color);
   };

   // random colored balls
   var obj_count = 3;
   var hue = Math.random()*360;
   var angle = Math.random()*Math.PI/8+Math.PI/6;
   for (var i=0; i<obj_count; ++i) {
      var radius = Math.random()*h/8+h/3;
      makeObj(hue,angle,radius);
      angle += Math.random()*Math.PI/4+Math.PI/8;
      hue += Math.random()*40+60;
      if (hue > 360) {
         hue -= 360;
      }
   }
}
