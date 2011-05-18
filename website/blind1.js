function Box(R,x,y,w,h,c) {
   this.x = x; // left
   this.y = y; // top
   this.w = w; // width
   this.h = h; // height
   this.rect = R.rect(x,y,w,h).attr({fill:c});
}

function Image(R,x1,y1,x2,y2,c) {
   this.x1 = x1;
   this.y1 = y1;
   this.x2 = x2;
   this.y2 = y2;
   this.path = R.path().attr({path:["M",x1,y1,"L",x2,y2],stroke:c});
}

function Cam(x,y) {
   this.x = x; // center x
   this.y = y; // center y
   this.r = 10; // radius
}

function Screen(tx,ty,sx,sy) {
   this.tx = tx;
   this.ty = ty;
   this.sx = sx;
   this.sy = sy;
   this.sin = 0;
   this.cos = 0;
}

Screen.prototype.update = function() {
   var dx=this.tx-this.sx;
   if (Math.abs(dx) < 0.0001)
   {
      this.sin = 0;
      this.cos = 1;
      return;
   }
   var dy=this.ty-this.sy;
   var len = Math.sqrt(dx*dx+dy*dy);
   this.sin = len / (dx+dy*dy/dx);
   this.cos = dy/dx*this.sin;
}

Screen.prototype.project(cam,px,py) {
   var x,y;

   // transform point
   x = px-cam.x;
   if (x < 0.0001)
   {
      // behind or too close to camera
      return null;
   }
   y = py-cam.y;
   var nx = x*this.cos - y*this.sin;
   var ny = x*this.sin + y*this.cos;

   // transform screen
   x = this.tx - cam.x;
   y = this.ty - cam.y;
   var sx = x*this.cos - y*this.sin; // screen x
   var sy = x*this.sin + y*this.cos; // screen top
   var sy2 = sy + this.len;          // screen bottom;

   var ny2 = ny/nx*sx;
   if (ny2 > sy2 || ny2 < sy)
   {
      // outside of screen bounds
      return null;
   }

   // unrotate
   x = sx*this.cos + ny2*this.sin;
   y = -sx*this.sin+ ny2*this.cos;

   // untranslate
   x += cam.x;
   y += cam.y;

   return [x,y];
}

window.onload = function() {
   var w = 400,
       h = 200,

       cam = { x:1, y:1 },
       screen = { x:1, y:1, height:1},
}
