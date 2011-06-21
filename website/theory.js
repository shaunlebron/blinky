(function() {
  /*
  ------------------------------------------------------
  
  NOTE:
  "theory.js" is compiled from "theory.coffee"
  
  TOOLS:
     - RaphaelJS
     - CoffeeScript
  
  DESCRIPTION:
  This script creates interactive figures for learning
  about different projections used in Quake Lenses.
  
  ------------------------------------------------------
  */  var Ball, Figure, FigureCircle, FigureRect, VScrollBar, bound, camIcon, populateFigure, sign;
  var __hasProp = Object.prototype.hasOwnProperty, __extends = function(child, parent) {
    for (var key in parent) { if (__hasProp.call(parent, key)) child[key] = parent[key]; }
    function ctor() { this.constructor = child; }
    ctor.prototype = parent.prototype;
    child.prototype = new ctor;
    child.__super__ = parent.prototype;
    return child;
  }, __bind = function(fn, me){ return function(){ return fn.apply(me, arguments); }; };
  bound = function(x, min, max) {
    return Math.min(Math.max(x, min), max);
  };
  sign = function(x) {
    var _ref;
    return (_ref = x < 0) != null ? _ref : -{
      1: 1
    };
  };
  camIcon = "M24.25,10.25H20.5v-1.5h-9.375v1.5h-3.75c-1.104,0-2,0.896-2,2v10.375c0,1.104,0.896,2,2,2H24.25c1.104,0,2-0.896,2-2V12.25C26.25,11.146,25.354,10.25,24.25,10.25zM15.812,23.499c-3.342,0-6.06-2.719-6.06-6.061c0-3.342,2.718-6.062,6.06-6.062s6.062,2.72,6.062,6.062C21.874,20.78,19.153,23.499,15.812,23.499zM15.812,13.375c-2.244,0-4.062,1.819-4.062,4.062c0,2.244,1.819,4.062,4.062,4.062c2.244,0,4.062-1.818,4.062-4.062C19.875,15.194,18.057,13.375,15.812,13.375z";
  Figure = (function() {
    function Figure(id, w, h) {
      this.id = id;
      this.w = w;
      this.h = h;
      this.R = Raphael(id, w, h);
      this.cam = {
        x: w / 2,
        y: h / 2 + 40,
        r: 5
      };
      this.cam.vis = this.R.path(camIcon).attr({
        fill: "#000",
        opacity: "0.8"
      });
      this.cam.vis.translate(this.cam.x - 16, this.cam.y - 10);
      this.aboveScreen = this.R.path();
    }
    return Figure;
  })();
  FigureRect = (function() {
    __extends(FigureRect, Figure);
    function FigureRect(id, w, h) {
      FigureRect.__super__.constructor.call(this, id, w, h);
      this.screen = {
        x: w / 2,
        y: h / 2 - 20,
        width: w * 0.8
      };
      this.screen.vis = this.R.path(["M", this.screen.x - this.screen.width / 2, this.screen.y, "h", this.screen.width]);
      this.screen.vis.attr({
        opacity: "0.5"
      }).insertBefore(this.aboveScreen);
    }
    FigureRect.prototype.updateBallImage = function(ball) {
      var ix1, ix2;
      ix1 = (ball.cx1 - this.cam.x) / (ball.cy1 - this.cam.y) * (this.screen.y - this.cam.y) + this.cam.x;
      ix2 = (ball.cx2 - this.cam.x) / (ball.cy2 - this.cam.y) * (this.screen.y - this.cam.y) + this.cam.x;
      if (ball.cy1 >= this.cam.y && ball.cy2 >= this.cam.y) {
        return ball.image.attr({
          path: ""
        });
      } else {
        if (ball.cy1 >= this.cam.y) {
          ix1 = -Infinity;
        } else if (ball.cy2 >= this.cam.y) {
          ix2 = Infinity;
        }
        ix1 = bound(ix1, this.screen.x - this.screen.width / 2, this.screen.x + this.screen.width / 2);
        ix2 = bound(ix2, this.screen.x - this.screen.width / 2, this.screen.x + this.screen.width / 2);
        return ball.image.attr({
          path: ["M", ix1, this.screen.y, "H", ix2]
        });
      }
    };
    return FigureRect;
  })();
  VScrollBar = (function() {
    function VScrollBar(x, y, height, value, R, f) {
      var onDragEnd, onDragMove, onDragStart;
      this.x = x;
      this.y = y;
      this.height = height;
      this.value = value;
      this.f = f;
      this.bar = R.path(["M", x, y - height / 2, "v", height]);
      this.bar.attr({
        opacity: "0.3"
      });
      this.button = {
        w: 30,
        h: 10
      };
      this.miny = y - height / 2;
      this.maxy = y + height / 2 - this.button.h;
      this.rangey = this.maxy - this.miny;
      this.button.vis = R.rect(x - this.button.w / 2, 0, this.button.w, this.button.h, 3);
      this.button.vis.attr({
        fill: "#333",
        stroke: "none",
        cursor: "move",
        opacity: "0.8"
      });
      onDragStart = __bind(function() {
        return this.oy = this.button.vis.attrs.y;
      }, this);
      onDragMove = __bind(function(dx, dy) {
        var ny;
        ny = bound(this.oy + dy, this.miny, this.maxy);
        this.value = (ny - this.miny) / this.rangey;
        return this.setValue(this.value);
      }, this);
      onDragEnd = function() {};
      this.button.vis.drag(onDragMove, onDragStart, onDragEnd);
      this.setValue(value);
    }
    VScrollBar.prototype.setValue = function(value) {
      this.value = bound(value, 0, 1);
      this.button.vis.attr({
        y: this.miny + this.rangey * this.value
      });
      return this.f(this.value);
    };
    return VScrollBar;
  })();
  FigureCircle = (function() {
    __extends(FigureCircle, Figure);
    function FigureCircle(id, w, h) {
      var onScroll, r;
      FigureCircle.__super__.constructor.call(this, id, w, h);
      this.screen = {
        x: this.cam.x,
        y: this.cam.y,
        r: 50,
        n: 40
      };
      this.screen.foldAngle = Math.PI - 2 * Math.PI / this.screen.n;
      r = this.screen.r;
      this.screen.segLength = Math.sqrt(2 * r * r * (1 - Math.cos(2 * Math.PI / this.screen.n)));
      this.screen.vis = this.R.path().attr({
        fill: "none",
        opacity: "0.5"
      });
      this.screen.vis.insertBefore(this.aboveScreen);
      this.da = Math.PI - this.screen.foldAngle;
      onScroll = __bind(function(scale) {
        return this.setPathFromFoldAngle(this.screen.foldAngle + this.da * (1 - scale));
      }, this);
      this.scroll = new VScrollBar(w - 50, h / 2, 100, 1, this.R, onScroll);
    }
    FigureCircle.prototype.updateBallImage = function(ball) {
      return ball.image.attr({
        path: ["M", this.cam.x + this.screen.r * Math.cos(ball.angle - ball.da), this.cam.y + this.screen.r * Math.sin(ball.angle - ball.da), "A", this.screen.r, this.screen.r, 0, 0, 1, this.cam.x + this.screen.r * Math.cos(ball.angle + ball.da), this.cam.y + this.screen.r * Math.sin(ball.angle + ball.da)]
      });
    };
    FigureCircle.prototype.setPathFromFoldAngle = function(angle) {
      var c, dx, dy, halfAngle, i, index0, index1, len, path, s, x0, x1, y0, y1, _ref;
      halfAngle = angle / 2;
      x0 = this.screen.x;
      y0 = this.screen.y - this.screen.r;
      x1 = x0 + this.screen.segLength * Math.sin(halfAngle);
      y1 = y0 + this.screen.segLength * Math.cos(halfAngle);
      len = 3 * (this.screen.n + 1);
      path = new Array(len);
      index0 = index1 = this.screen.n / 2 * 3;
      path[index0] = "L";
      path[index0 + 1] = x0;
      path[index0 + 2] = y0;
      index0 -= 3;
      index1 += 3;
      path[index0] = path[index1] = "L";
      path[index0 + 1] = 2 * this.screen.x - x1;
      path[index1 + 1] = x1;
      path[index0 + 2] = path[index1 + 2] = y1;
      s = Math.sin(-angle);
      c = Math.cos(-angle);
      for (i = 2, _ref = this.screen.n / 2; 2 <= _ref ? i <= _ref : i >= _ref; 2 <= _ref ? i++ : i--) {
        dx = x0 - x1;
        dy = y0 - y1;
        x0 = x1;
        y0 = y1;
        x1 = x1 + dx * c - dy * s;
        y1 = y1 + dx * s + dy * c;
        index0 -= 3;
        index1 += 3;
        path[index0] = path[index1] = "L";
        path[index0 + 1] = 2 * this.screen.x - x1;
        path[index1 + 1] = x1;
        path[index0 + 2] = path[index1 + 2] = y1;
      }
      path[0] = "M";
      return this.screen.vis.attr({
        path: path
      });
    };
    return FigureCircle;
  })();
  Ball = (function() {
    function Ball(x, y, r, color, figure) {
      var touchDragEnd, touchDragMove, touchDragStart;
      this.x = x;
      this.y = y;
      this.r = r;
      this.color = color;
      this.figure = figure;
      this.circle = this.figure.R.circle(x, y, r).attr({
        fill: color,
        stroke: "none"
      });
      this.image = this.figure.R.path().attr({
        "stroke-width": "5px",
        stroke: this.color
      });
      this.cone = this.figure.R.path().attr({
        fill: this.color,
        opacity: "0.2",
        stroke: "none"
      });
      this.bringAboveScreen();
      touchDragMove = __bind(function(dx, dy) {
        this.x = bound(this.ox + dx, 0, this.figure.w);
        this.y = bound(this.oy + dy, 0, this.figure.h);
        if (!this.update()) {
          r = this.figure.cam.r + this.r + 0.1;
          this.x = this.figure.cam.x + this.dx / this.dist * r;
          this.y = this.figure.cam.y + this.dy / this.dist * r;
          this.update();
        }
        this.circle.attr({
          cx: this.x,
          cy: this.y
        });
        return this.touch.attr({
          cx: this.x,
          cy: this.y
        });
      }, this);
      touchDragStart = __bind(function() {
        this.ox = this.touch.attrs.cx;
        this.oy = this.touch.attrs.cy;
        return this.bringAboveScreen();
      }, this);
      touchDragEnd = __bind(function() {}, this);
      this.touch = this.figure.R.circle(x, y, r).attr({
        fill: "#000",
        stroke: "none",
        opacity: "0",
        cursor: "move"
      }).drag(touchDragMove, touchDragStart, touchDragEnd);
      this.update();
    }
    Ball.prototype.bringAboveScreen = function() {
      this.circle.insertBefore(this.figure.aboveScreen);
      this.image.insertBefore(this.figure.aboveScreen);
      return this.cone.insertBefore(this.figure.aboveScreen);
    };
    Ball.prototype.updateCone = function() {
      var t;
      this.angle = Math.atan2(this.dy, this.dx);
      this.da = Math.asin(this.r / this.dist);
      t = this.figure.w * this.figure.h;
      this.cx1 = this.figure.cam.x + t * Math.cos(this.angle - this.da);
      this.cy1 = this.figure.cam.y + t * Math.sin(this.angle - this.da);
      this.cx2 = this.figure.cam.x + t * Math.cos(this.angle + this.da);
      this.cy2 = this.figure.cam.y + t * Math.sin(this.angle + this.da);
      return this.cone.attr({
        path: ["M", this.figure.cam.x, this.figure.cam.y, "L", this.cx1, this.cy1, "L", this.cx2, this.cy2, "Z"]
      });
    };
    Ball.prototype.update = function() {
      this.dx = this.x - this.figure.cam.x;
      this.dy = this.y - this.figure.cam.y;
      this.dist = Math.sqrt(this.dx * this.dx + this.dy * this.dy);
      if (this.dist <= this.r + this.figure.cam.r) {
        return false;
      } else {
        this.updateCone();
        this.figure.updateBallImage(this);
        return true;
      }
    };
    Ball.prototype.create = function(hue, angle, dist, radius, figure) {
      var color;
      color = "hsl( " + hue + " ,60, 50)";
      return new Ball(figure.cam.x + Math.cos(angle) * dist, figure.cam.y - Math.sin(angle) * dist, radius, color, figure);
    };
    return Ball;
  })();
  populateFigure = function(figure) {
    var angle, dist, hue, i, obj_count, _ref, _results;
    obj_count = 3;
    hue = Math.random() * 360;
    angle = Math.random() * Math.PI / 8 + Math.PI / 6;
    _results = [];
    for (i = 0, _ref = obj_count - 1; 0 <= _ref ? i <= _ref : i >= _ref; 0 <= _ref ? i++ : i--) {
      dist = Math.random() * figure.h / 8 + figure.h / 3;
      Ball.prototype.create(hue, angle, dist, 20, figure);
      hue += Math.random() * 40 + 60;
      if (hue > 360) {
        hue -= 360;
      }
      _results.push(angle += Math.random() * Math.PI / 4 + Math.PI / 8);
    }
    return _results;
  };
  window.onload = function() {
    populateFigure(new FigureRect("figure1", 650, 300));
    return populateFigure(new FigureCircle("figure2", 650, 300));
  };
}).call(this);
