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

      @balls = []

   projectBalls: ->
      @projectBall ball for ball in @balls

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

   projectBall: (ball) ->
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


# a function to construct a vertical scrollbar
class VScrollBar
   constructor: (@x,@y,@height,@value,R, @f) ->
      @bar = R.path ["M",x,y-height/2,"v",height]
      @bar.attr opacity:"0.3"

      @button =
         w : 30
         h : 10

      @miny = y-height/2
      @maxy = y+height/2-@button.h
      @rangey = @maxy-@miny

      @button.vis = R.rect x-@button.w/2,0,@button.w,@button.h, 3
      @button.vis.attr fill:"#333", stroke:"none", cursor:"move", opacity:"0.8"
      onDragStart = =>
         @oy = @button.vis.attrs.y
      onDragMove = (dx,dy) =>
         ny = bound @oy+dy, @miny, @maxy
         @value = (ny - @miny) / @rangey
         @setValue @value
      onDragEnd = ->
      @button.vis.drag(onDragMove, onDragStart, onDragEnd)

      @setValue value

   setValue: (value) ->
      @value = bound value, 0, 1
      @button.vis.attr y : @miny + @rangey * @value
      @f @value

# Figure class for the panoramic projection
class FigureCircle extends Figure
   constructor: (id,w,h) ->
      super id,w,h

      # the screen is an n-sided regular polygon
      @screen = 
         x : @cam.x # center
         y : @cam.y
         r : 50  # radius
         n : 40 # number of segments

      # The fold angle is the angle between contiguous segments
      # such that they form a closed polygon.
      # This angle will be transitioned to Math.PI (180 degrees)
      # in order to flatten the screen.
      @screen.foldAngle = Math.PI - 2*Math.PI / @screen.n
      r = @screen.r
      @screen.segLength = Math.sqrt(2*r*r*(1-Math.cos(2*Math.PI/@screen.n)))

      @screen.vis = @R.path().attr(fill:"none",opacity:"0.5")
      @screen.vis.insertBefore(@aboveScreen)

      @da = Math.PI-@screen.foldAngle
      onScroll = (scale) =>
         @setPathFromFoldAngle(@screen.foldAngle+@da*(1-scale))

      @scroll = new VScrollBar(w - 50, h/2, 100, 1, @R, onScroll)

   projectBall: (ball) ->
      #ball.image.attr path:[
      #    "M",
      #    @cam.x + @screen.r * Math.cos(ball.angle-ball.da),
      #    @cam.y + @screen.r * Math.sin(ball.angle-ball.da),
      #    "A",
      #    @screen.r, @screen.r, 0, 0, 1,
      #    @cam.x + @screen.r * Math.cos(ball.angle+ball.da),
      #    @cam.y + @screen.r * Math.sin(ball.angle+ball.da) ]
      minAngle = ball.angle-ball.da
      maxAngle = ball.angle+ball.da
      minAngle -= Math.PI/2
      maxAngle -= Math.PI/2
      if minAngle < 0 < maxAngle
         # split path into two images
         minAngle += Math.PI*2 if minAngle < 0
         maxAngle += Math.PI*2 if maxAngle < 0
         path = @screen.vis.getSubpath(0, maxAngle*@screen.r)
         path2 = @screen.vis.getSubpath(minAngle*@screen.r, 2*Math.PI*@screen.r-1)
         path += path2
      else
         minAngle += Math.PI*2 if minAngle < 0
         maxAngle += Math.PI*2 if maxAngle < 0
         path = @screen.vis.getSubpath minAngle*@screen.r, maxAngle*@screen.r

      ball.image.attr path:path

   setPathFromFoldAngle: (angle) ->
      halfAngle = angle/2

      # calculate the top of the circle and the immediate point to the right
      dx = @screen.segLength * Math.sin halfAngle
      dy = @screen.segLength * Math.cos halfAngle
      @rfold = (dx*dx+dy*dy)/(2*dy)

      # I hate this more than anything in my life

      path = []

      if Math.abs(angle - @screen.foldAngle) < 0.001
         dt = 2*Math.PI / @screen.n
         for i in [0..@screen.n-1]
            path.push("L", @screen.x + @screen.r * Math.cos(Math.PI/2 + dt*i), 
                           @screen.y + @screen.r * Math.sin(Math.PI/2 + dt*i))
         path[0] = "M"
         path.push "Z"
      else if dy < 0.001
         path = ["M", @screen.x-Math.PI*@screen.r,@screen.y-@screen.r, "h", 2*Math.PI*@screen.r]

      else
         x0 = @screen.x
         y0 = @screen.y - @screen.r
         x1 = x0+dx
         y1 = y0+dy

         # add the rest of the points
         s = Math.sin(-angle)
         c = Math.cos(-angle)
         for i in [2..@screen.n/2]
            dx = x0-x1
            dy = y0-y1
            x0 = x1
            y0 = y1
            x1 = x1 + dx*c - dy*s
            y1 = y1 + dx*s + dy*c

         # critical angle for switching large arc sweep
         ca = @da/2 + @screen.foldAngle

         if angle < ca
            path = ["M",x1,y1,"A",@rfold,@rfold,0,1,0,2*@screen.x-x1,y1]
         else
            path = ["M",x1,y1,"A",@rfold,@rfold,0,0,0,2*@screen.x-x1,y1]

      # update the path
      @screen.vis.attr path:path

      @projectBalls()


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
         @figure.projectBall @
         true

   create: (hue, angle, dist, radius, figure) ->
      color = "hsl( #{hue} ,60, 50)"
      figure.balls.push new Ball \
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
