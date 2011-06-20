###
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
###

# math helpers
bound = (x,min,max) -> Math.min Math.max(x,min), max
sign = (x) -> (x < 0) ? -1 : 1

# RaphaelJS icon path strings
camIcon =
   "M24.25,10.25H20.5v-1.5h-9.375v1.5h-3.75c-1.104,0-2,0.896-2,2v10.375c0,1.104,0.896,2,2,2H24.25c1.104,0,2-0.896,2-2V12.25C26.25,11.146,25.354,10.25,24.25,10.25zM15.812,23.499c-3.342,0-6.06-2.719-6.06-6.061c0-3.342,2.718-6.062,6.06-6.062s6.062,2.72,6.062,6.062C21.874,20.78,19.153,23.499,15.812,23.499zM15.812,13.375c-2.244,0-4.062,1.819-4.062,4.062c0,2.244,1.819,4.062,4.062,4.062c2.244,0,4.062-1.818,4.062-4.062C19.875,15.194,18.057,13.375,15.812,13.375z"

# common Figure class
class Figure
   constructor: (@id, @w, @h) ->
      @R = Raphael id, w, h

      @cam =
         x : w/2
         y : h/2+40
         r : 5

      @cam.vis = @R.path(camIcon).attr(fill:"#000", opacity:"0.8")
      @cam.vis.translate(@cam.x-16, @cam.y-10)

      @aboveScreen = @R.path()

# Figure class for the rectilinear projection
class FigureRect extends Figure
   constructor: (id,w,h) ->
      super id,w,h

      @screen =
         x : w/2
         y : h/2-20
         width : w*0.8
      @screen.vis = @R.path ["M", @screen.x - @screen.width/2, @screen.y, "h", @screen.width]
      @screen.vis.attr(opacity:"0.5").insertBefore(@aboveScreen)

   updateBallImage: (ball) ->
      ix1 = (ball.cx1-@cam.x) / (ball.cy1-@cam.y) * (@screen.y-@cam.y) + @cam.x
      ix2 = (ball.cx2-@cam.x) / (ball.cy2-@cam.y) * (@screen.y-@cam.y) + @cam.x

      if ball.cy1 >= @cam.y and ball.cy2 >= @cam.y
         # ball is behind camera
         ball.image.attr path:""
      else
         # determine infinite distance cases
         if ball.cy1 >= @cam.y
            ix1 = -Infinity
         else if ball.cy2 >= @cam.y
            ix2 = Infinity

         # bound to screen
         ix1 = bound ix1, @screen.x - @screen.width/2, @screen.x+@screen.width/2
         ix2 = bound ix2, @screen.x - @screen.width/2, @screen.x+@screen.width/2

         # update screen image
         ball.image.attr path:[ "M",ix1,@screen.y, "H",ix2 ]

# Figure class for the panoramic projection
class FigureCircle extends Figure
   constructor: (id,w,h) ->
      super id,w,h

      @screen = r : 50
      @screen.vis = @R.circle(@cam.x, @cam.y, @screen.r).attr(fill:"none",opacity:"0.5")
      @screen.vis.insertBefore(@aboveScreen)

   updateBallImage: (ball) ->
      ball.image.attr path:[
          "M",
          @cam.x + @screen.r * Math.cos(ball.angle-ball.da),
          @cam.y + @screen.r * Math.sin(ball.angle-ball.da),
          "A",
          @screen.r, @screen.r, 0, 0, 1,
          @cam.x + @screen.r * Math.cos(ball.angle+ball.da),
          @cam.y + @screen.r * Math.sin(ball.angle+ball.da) ]

# a ball to be projected onto a screen
class Ball
   constructor: (@x,@y,@r,@color,@figure) ->

      @circle = @figure.R.circle(x,y,r).attr fill:color, stroke:"none"
      @image = @figure.R.path().attr "stroke-width":"5px", stroke:@color
      @cone = @figure.R.path().attr fill:@color, opacity:"0.2", stroke:"none"
      @bringAboveScreen()

      touchDragMove = (dx,dy) =>
         @x = bound @ox + dx, 0, @figure.w
         @y = bound @oy + dy, 0, @figure.h

         if not @update()
            r = @figure.cam.r + @r + 0.1
            @x = @figure.cam.x + @dx/@dist * r
            @y = @figure.cam.y + @dy/@dist * r
            @update()

         @circle.attr cx:@x, cy:@y
         @touch.attr cx:@x, cy:@y

      touchDragStart = =>
         @ox = @touch.attrs.cx
         @oy = @touch.attrs.cy
         @bringAboveScreen()

      touchDragEnd = =>

      @touch = @figure.R.circle(x,y,r)
         .attr(fill:"#000",stroke:"none",opacity:"0",cursor:"move")
         .drag(touchDragMove, touchDragStart, touchDragEnd)

      @update()

   bringAboveScreen: ->
      @circle.insertBefore @figure.aboveScreen
      @image.insertBefore @figure.aboveScreen
      @cone.insertBefore @figure.aboveScreen

   updateCone: ->
      # set cone position
      @angle = Math.atan2 @dy, @dx
      @da = Math.asin @r/@dist
      t = @figure.w * @figure.h # arbitrarily large (relies on clipping)

      @cx1 = @figure.cam.x + t * Math.cos @angle - @da
      @cy1 = @figure.cam.y + t * Math.sin @angle - @da
      @cx2 = @figure.cam.x + t * Math.cos @angle + @da
      @cy2 = @figure.cam.y + t * Math.sin @angle + @da

      @cone.attr path:[
         "M", @figure.cam.x, @figure.cam.y,
         "L", @cx1, @cy1,
         "L", @cx2, @cy2,
         "Z"]

   update: ->
      @dx = @x - @figure.cam.x
      @dy = @y - @figure.cam.y
      @dist = Math.sqrt @dx*@dx + @dy*@dy

      if @dist <= @r + @figure.cam.r
         false
      else
         @updateCone()
         @figure.updateBallImage @
         true

   create: (hue, angle, dist, radius, figure) ->
      color = "hsl( #{hue} ,60, 50)"
      new Ball \
         figure.cam.x + Math.cos(angle)*dist,
         figure.cam.y - Math.sin(angle)*dist,
         radius,
         color,
         figure

# populate the figure with colored balls
populateFigure = (figure) ->
   obj_count = 3
   hue = Math.random()*360
   angle = Math.random()*Math.PI/8+Math.PI/6

   for i in [0..obj_count-1]
      dist = Math.random()*figure.h/8+figure.h/3
      Ball::create hue,angle,dist,20,figure

      hue += Math.random()*40+60
      hue -= 360 if hue > 360
      angle += Math.random()*Math.PI/4+Math.PI/8

# create the figures
window.onload = ->
   populateFigure new FigureRect "figure1", 650, 300
   populateFigure new FigureCircle "figure2", 650, 300
