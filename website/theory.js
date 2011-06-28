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
  */  var Ball, Figure, FigureCircle, FigureRect, FigureStereo, bound, camIcon, populateFigure, sign;
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
      this.cam1 = {
        x: w / 2,
        y: h / 2 + 40,
        r: 5
      };
      this.cam1.vis = this.R.path(camIcon).attr({
        fill: "#000",
        opacity: "0.8"
      });
      this.cam1.vis.translate(this.cam1.x - 16, this.cam1.y - 10);
      this.cam = this.cam1;
      this.aboveScreen = this.R.path();
      this.balls = [];
      this.yoyo = this.R.path();
    }
    Figure.prototype.projectBalls = function() {
      var ball, _i, _len, _ref, _results;
      _ref = this.balls;
      _results = [];
      for (_i = 0, _len = _ref.length; _i < _len; _i++) {
        ball = _ref[_i];
        _results.push(this.projectBall(ball));
      }
      return _results;
    };
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
        "stroke-width": "10px",
        opacity: "0.1"
      }).insertBefore(this.aboveScreen);
    }
    FigureRect.prototype.projectBall = function(ball) {
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
  FigureCircle = (function() {
    __extends(FigureCircle, Figure);
    function FigureCircle(id, w, h) {
      var r;
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
        "stroke-width": "10px",
        fill: "none",
        opacity: "0.1"
      });
      this.screen.vis.insertBefore(this.aboveScreen);
      this.da = Math.PI - this.screen.foldAngle;
      this.foldScreen(this.screen.foldAngle + this.da);
      this.yoyo.attr({
        x: 1
      });
      this.yoyo.onAnimation(__bind(function() {
        return this.foldScreen(this.screen.foldAngle + this.da * this.yoyo.attr("x"));
      }, this));
    }
    FigureCircle.prototype.ballDragStart = function() {
      var ball, _i, _len, _ref;
      _ref = this.balls;
      for (_i = 0, _len = _ref.length; _i < _len; _i++) {
        ball = _ref[_i];
        ball.cone.attr({
          opacity: "0.1"
        });
      }
      return this.yoyo.animate({
        x: 0
      }, 200, __bind(function() {
        return this.foldScreen(this.screen.foldAngle);
      }, this));
    };
    FigureCircle.prototype.ballDragEnd = function() {
      var ball, _i, _len, _ref;
      _ref = this.balls;
      for (_i = 0, _len = _ref.length; _i < _len; _i++) {
        ball = _ref[_i];
        ball.cone.attr({
          opacity: "0"
        });
      }
      return this.yoyo.animate({
        x: 1
      }, 200, __bind(function() {
        return this.foldScreen(this.screen.foldAngle + this.da);
      }, this));
    };
    FigureCircle.prototype.projectBall = function(ball) {
      var maxAngle, minAngle, path, start, _ref, _ref2;
      minAngle = ball.angle - ball.da;
      maxAngle = ball.angle + ball.da;
      minAngle -= Math.PI / 2;
      maxAngle -= Math.PI / 2;
      if ((minAngle < 0 && 0 < maxAngle)) {
        if (minAngle < 0) {
          minAngle += Math.PI * 2;
        }
        if (maxAngle < 0) {
          maxAngle += Math.PI * 2;
        }
        if (maxAngle < minAngle) {
          _ref = [maxAngle, minAngle], minAngle = _ref[0], maxAngle = _ref[1];
        }
        if (this.arcAngle < 0.001) {
          path = ["M", this.screen.x - Math.PI * this.screen.r, this.screen.y - this.screen.r, "H", this.screen.x - Math.PI * this.screen.r + minAngle * this.screen.r, "M", this.screen.x - Math.PI * this.screen.r + maxAngle * this.screen.r, this.screen.y - this.screen.r, "H", this.screen.x + Math.PI * this.screen.r];
        } else {
          minAngle = minAngle / (2 * Math.PI) * this.arcAngle;
          maxAngle = maxAngle / (2 * Math.PI) * this.arcAngle;
          if (maxAngle < minAngle) {
            _ref2 = [maxAngle, minAngle], minAngle = _ref2[0], maxAngle = _ref2[1];
          }
          start = (2 * Math.PI - this.arcAngle) / 2 + Math.PI / 2;
          path = ["M", this.arcCenterX + this.arcRadius * Math.cos(start), this.arcCenterY + this.arcRadius * Math.sin(start), "A", this.arcRadius, this.arcRadius, 0, 0, 1, this.arcCenterX + this.arcRadius * Math.cos(start + minAngle), this.arcCenterY + this.arcRadius * Math.sin(start + minAngle), "M", this.arcCenterX + this.arcRadius * Math.cos(start + maxAngle), this.arcCenterY + this.arcRadius * Math.sin(start + maxAngle), "A", this.arcRadius, this.arcRadius, 0, 0, 1, this.arcCenterX + this.arcRadius * Math.cos(start + this.arcAngle), this.arcCenterY + this.arcRadius * Math.sin(start + this.arcAngle)];
        }
      } else {
        if (minAngle < 0) {
          minAngle += Math.PI * 2;
        }
        if (maxAngle < 0) {
          maxAngle += Math.PI * 2;
        }
        if (this.arcAngle < 0.001) {
          path = ["M", this.screen.x - Math.PI * this.screen.r + minAngle * this.screen.r, this.screen.y - this.screen.r, "H", this.screen.x - Math.PI * this.screen.r + maxAngle * this.screen.r];
        } else {
          minAngle = minAngle / (2 * Math.PI) * this.arcAngle;
          maxAngle = maxAngle / (2 * Math.PI) * this.arcAngle;
          start = (2 * Math.PI - this.arcAngle) / 2 + Math.PI / 2;
          path = ["M", this.arcCenterX + this.arcRadius * Math.cos(start + minAngle), this.arcCenterY + this.arcRadius * Math.sin(start + minAngle), "A", this.arcRadius, this.arcRadius, 0, 0, 1, this.arcCenterX + this.arcRadius * Math.cos(start + maxAngle), this.arcCenterY + this.arcRadius * Math.sin(start + maxAngle)];
        }
      }
      return ball.image.attr({
        path: path
      });
    };
    FigureCircle.prototype.foldScreen = function(angle) {
      var ca, dt, dx, dy, i, path, start, x0, x1, y0, y1, _ref;
      dx = this.screen.segLength * Math.sin(angle / 2);
      dy = this.screen.segLength * Math.cos(angle / 2);
      this.arcRadius = (dx * dx + dy * dy) / (2 * dy);
      this.arcAngle = 2 * Math.PI * this.screen.r / this.arcRadius;
      this.arcCenterX = this.screen.x;
      this.arcCenterY = this.screen.y - this.screen.r + this.arcRadius;
      path = [];
      if (Math.abs(angle - this.screen.foldAngle) < 0.001) {
        dt = 2 * Math.PI / this.screen.n;
        for (i = 0, _ref = this.screen.n - 1; 0 <= _ref ? i <= _ref : i >= _ref; 0 <= _ref ? i++ : i--) {
          path.push("L", this.screen.x + this.screen.r * Math.cos(Math.PI / 2 + dt * i), this.screen.y + this.screen.r * Math.sin(Math.PI / 2 + dt * i));
        }
        path[0] = "M";
        path.push("Z");
      } else if (dy < 0.001) {
        path = ["M", this.screen.x - Math.PI * this.screen.r, this.screen.y - this.screen.r, "h", 2 * Math.PI * this.screen.r];
      } else {
        ca = this.da / 2 + this.screen.foldAngle;
        start = (2 * Math.PI - this.arcAngle) / 2 + Math.PI / 2;
        x1 = this.arcCenterX + this.arcRadius * Math.cos(start);
        y1 = this.arcCenterY + this.arcRadius * Math.sin(start);
        x0 = this.arcCenterX + this.arcRadius * Math.cos(start + this.arcAngle);
        y0 = this.arcCenterY + this.arcRadius * Math.sin(start + this.arcAngle);
        if (angle < ca) {
          path = ["M", x0, y0, "A", this.arcRadius, this.arcRadius, 0, 1, 0, x1, y1];
        } else {
          path = ["M", x0, y0, "A", this.arcRadius, this.arcRadius, 0, 0, 0, x1, y1];
        }
      }
      this.screen.vis.attr({
        path: path
      });
      return this.projectBalls();
    };
    return FigureCircle;
  })();
  FigureStereo = (function() {
    __extends(FigureStereo, Figure);
    function FigureStereo(id, w, h) {
      FigureStereo.__super__.constructor.call(this, id, w, h);
      this.screen1 = {
        x: this.cam.x,
        y: this.cam.y,
        r: 50
      };
      this.screen1.vis = this.R.circle(this.screen1.x, this.screen1.y, this.screen1.r);
      this.screen1.vis.attr({
        "stroke-width": "10px",
        fill: "none",
        opacity: "0.1"
      });
      this.screen1.vis.insertBefore(this.aboveScreen);
      this.cam2 = {
        x: this.cam.x,
        y: this.cam.y + this.screen1.r,
        r: 5
      };
      this.cam2.vis = this.R.path(camIcon).attr({
        fill: "#000",
        opacity: "0.8"
      });
      this.cam2.vis.translate(this.cam2.x - 16, this.cam2.y - 10);
      this.screen2 = {
        x: w / 2,
        y: h / 2 - 20,
        width: w * 0.8
      };
      this.screen2.vis = this.R.path(["M", this.screen2.x - this.screen2.width / 2, this.screen2.y, "h", this.screen2.width]);
      this.screen2.vis.attr({
        "stroke-width": "10px",
        opacity: "0.1"
      }).insertBefore(this.aboveScreen);
      this.screen = this.screen1;
    }
    FigureStereo.prototype.ballDragStart = function() {
      this.screen = this.screen1;
      this.cam1.vis.attr({
        opacity: "0.8"
      });
      this.cam2.vis.attr({
        opacity: "0.1"
      });
      return this.projectBalls();
    };
    FigureStereo.prototype.ballDragEnd = function() {
      this.screen = this.screen2;
      this.cam1.vis.attr({
        opacity: "0.1"
      });
      this.cam2.vis.attr({
        opacity: "0.8"
      });
      return this.projectBalls();
    };
    FigureStereo.prototype.projectBall = function(ball) {
      var cx1, cx2, cy1, cy2, ix1, ix1b, ix2, ix2b;
      cx1 = this.screen1.x + this.screen1.r * Math.cos(ball.angle - ball.da);
      cy1 = this.screen1.y + this.screen1.r * Math.sin(ball.angle - ball.da);
      cx2 = this.screen1.x + this.screen1.r * Math.cos(ball.angle + ball.da);
      cy2 = this.screen1.y + this.screen1.r * Math.sin(ball.angle + ball.da);
      ix1 = (cx1 - this.cam2.x) / (cy1 - this.cam2.y) * (this.screen2.y - this.cam2.y) + this.cam2.x;
      ix2 = (cx2 - this.cam2.x) / (cy2 - this.cam2.y) * (this.screen2.y - this.cam2.y) + this.cam2.x;
      ix1b = bound(ix1, this.screen2.x - this.screen2.width / 2, this.screen2.x + this.screen2.width / 2);
      ix2b = bound(ix2, this.screen2.x - this.screen2.width / 2, this.screen2.x + this.screen2.width / 2);
      ball.image.attr({
        path: ["M", cx1, cy1, "A", this.screen1.r, this.screen1.r, 0, 0, 1, cx2, cy2, "M", ix1b, this.screen2.y, "H", ix2b]
      });
      if (this.screen === this.screen1) {
        return ball.cone.attr({
          path: ["M", this.cam1.x, this.cam1.y, "L", ball.cx1, ball.cy1, "L", ball.cx2, ball.cy2, "Z"]
        });
      } else {
        return ball.cone.attr({
          path: ["M", this.cam2.x, this.cam2.y, "L", ix1, this.screen2.y, "L", ix2, this.screen2.y, "Z"]
        });
      }
    };
    return FigureStereo;
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
        opacity: "0.1",
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
        this.bringAboveScreen();
        if (this.figure.ballDragStart != null) {
          return this.figure.ballDragStart();
        }
      }, this);
      touchDragEnd = __bind(function() {
        if (this.figure.ballDragEnd != null) {
          return this.figure.ballDragEnd();
        }
      }, this);
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
        this.figure.projectBall(this);
        return true;
      }
    };
    Ball.prototype.create = function(hue, angle, dist, radius, figure) {
      var color;
      color = "hsl( " + hue + " ,60, 50)";
      return figure.balls.push(new Ball(figure.cam.x + Math.cos(angle) * dist, figure.cam.y - Math.sin(angle) * dist, radius, color, figure));
    };
    return Ball;
  })();
  populateFigure = function(figure, obj_count) {
    var angle, dist, hue, i, _ref, _results;
    if (obj_count == null) {
      obj_count = 1;
    }
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
    populateFigure(new FigureCircle("figure2", 650, 300));
    return populateFigure(new FigureStereo("figure3", 650, 300));
  };
}).call(this);
