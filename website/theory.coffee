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
      @screen.vis.attr("stroke-width":"10px",opacity:"0.1").insertBefore(@aboveScreen)

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

      @screen.vis = @R.path().attr "stroke-width":"10px", fill:"none",opacity:"0.1"
      @screen.vis.insertBefore(@aboveScreen)

      @da = Math.PI-@screen.foldAngle
      onScroll = (scale) =>
         @foldScreen(@screen.foldAngle+@da*(1-scale))

      @scroll = new VScrollBar(w - 50, h/2, 100, 1, @R, onScroll)

   projectBall: (ball) ->

      # get the boundary angles of the projection
      minAngle = ball.angle-ball.da
      maxAngle = ball.angle+ball.da

      # rotate the frame such that the origin is at the bottom of the circle
      # positive angle is clockwise
      minAngle -= Math.PI/2
      maxAngle -= Math.PI/2

      # if the projection falls on a fault, it must be split
      if minAngle < 0 < maxAngle
         # TODO: split path into two images
         #minAngle += Math.PI*2 if minAngle < 0
         #maxAngle += Math.PI*2 if maxAngle < 0
         #minAngle = @toArcAngle minAngle
         #maxAngle = @toArcAngle maxAngle
         #path = @screen.vis.getSubpath(0, maxAngle*@screen.r)
         #path2 = @screen.vis.getSubpath(minAngle*@screen.r, 2*Math.PI*@screen.r-1)
         #path += path2

      # projection is contiguous
      else
         # make angles are positive
         minAngle += Math.PI*2 if minAngle < 0
         maxAngle += Math.PI*2 if maxAngle < 0

         # screen is horizontal
         if @arcAngle < 0.001
            path = [
               "M",
               @screen.x - Math.PI*@screen.r + minAngle*@screen.r,
               @screen.y - @screen.r,
               "H",
               @screen.x - Math.PI*@screen.r + maxAngle*@screen.r]

         # screen is curved
         else
            # convert circle angle to the arc angle
            minAngle = minAngle / (2*Math.PI) * @arcAngle
            maxAngle = maxAngle / (2*Math.PI) * @arcAngle

            # starting angle for the arc
            start = (2*Math.PI - @arcAngle)/2 + Math.PI/2
            path = [
               "M",
               @arcCenterX + @arcRadius * Math.cos(start + minAngle),
               @arcCenterY + @arcRadius * Math.sin(start + minAngle),
               "A",
               @arcRadius, @arcRadius, 
               0, # rotation
               0, # large sweep
               1, # arc sweep
               @arcCenterX + @arcRadius * Math.cos(start + maxAngle),
               @arcCenterY + @arcRadius * Math.sin(start + maxAngle)]

      # update projection
      ball.image.attr path:path

   foldScreen: (angle) ->
      # calculate the top of the circle and the immediate point to the right
      dx = @screen.segLength * Math.sin angle/2
      dy = @screen.segLength * Math.cos angle/2

      # the unfolded circle is actually an arc on a larger circle
      # with the following radius and angle coverage
      @arcRadius = (dx*dx+dy*dy)/(2*dy)
      @arcAngle = 2*Math.PI*@screen.r / @arcRadius
      @arcCenterX = @screen.x
      @arcCenterY = @screen.y - @screen.r + @arcRadius

      path = []

      # screen is a circle
      if Math.abs(angle - @screen.foldAngle) < 0.001
         dt = 2*Math.PI / @screen.n
         for i in [0..@screen.n-1]
            path.push("L", @screen.x + @screen.r * Math.cos(Math.PI/2 + dt*i), 
                           @screen.y + @screen.r * Math.sin(Math.PI/2 + dt*i))
         path[0] = "M"
         path.push "Z"

      # screen is horizontal
      else if dy < 0.001
         path = ["M", @screen.x-Math.PI*@screen.r,@screen.y-@screen.r, "h", 2*Math.PI*@screen.r]

      # screen is an arc
      else
         # critical angle for switching large arc sweep
         ca = @da/2 + @screen.foldAngle

         # arc points
         start = (2*Math.PI - @arcAngle)/2 + Math.PI/2
         x1 = @arcCenterX + @arcRadius * Math.cos(start)
         y1 = @arcCenterY + @arcRadius * Math.sin(start)
         x0 = @arcCenterX + @arcRadius * Math.cos(start+@arcAngle)
         y0 = @arcCenterY + @arcRadius * Math.sin(start+@arcAngle)

         if angle < ca
            path = ["M",x0,y0,"A",@arcRadius,@arcRadius,0,1,0,x1,y1]
         else
            path = ["M",x0,y0,"A",@arcRadius,@arcRadius,0,0,0,x1,y1]

      # update the screen shape path
      @screen.vis.attr path:path

      # reproject the balls on the new screen
      @projectBalls()


# a ball to be projected onto a screen
class Ball
   constructor: (@x,@y,@r,@color,@figure) ->

      @circle = @figure.R.circle(x,y,r).attr fill:color, stroke:"none"
      @image = @figure.R.path().attr "stroke-width":"5px", stroke:@color
      @cone = @figure.R.path().attr fill:@color, opacity:"0.1", stroke:"none"
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

      # create and add a ball to the given figure
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
