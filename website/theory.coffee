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

coneOpacity = 0.1
coneFadeSpeed = 200

screenFoldSpeed = 500

camFadeSpeed = 200
camOpacityMax = 0.8
camOpacityMin = 0.2

screenAttr = "stroke-width":"10px", opacity:"0.1"
camAttr = fill:"#000", opacity:camOpacityMax

imageThickness = 5
objectRadius = 20

# common Figure class
class Figure
   constructor: (@id, @w, @h) ->

      # RaphaelJS context
      @R = Raphael id, w, h

      # camera in the center of the figure
      @cam1 =
         x : w/2
         y : h/2+40
         r : 5

      # visual camera icon
      @cam1.vis = @R.path(camIcon).attr camAttr
      @cam1.vis.translate(@cam1.x-16, @cam1.y-10)

      # set primary cam to cam1 (used by Ball to decide origin of viewing cone)
      # (FigureStereo will have a second cam)
      @cam = @cam1

      # an empty object acting as a bookmark for the top of the screen
      @aboveScreen = @R.path()

      # list of balls to be projected to a screen
      @balls = []

      # blank path object used solely for hooking its animation callback
      # (we use it for animating arbitrary values
      #  like the folding angle in FigureCircle)
      @yoyo = @R.path()

   # update all the ball projection images
   projectBalls: ->
      @projectBall ball for ball in @balls

   # fade in viewing cones
   fadeInCones: ->
      for ball in @balls
         ball.cone.attr opacity:0
         ball.cone.animate({opacity:coneOpacity},coneFadeSpeed)

   fadeOutCones: ->
      for ball in @balls
         ball.cone.animate({opacity:0},coneFadeSpeed)

   # populate the figure with colored balls
   populate: (obj_count=1) ->
      hue = Math.random()*360
      angle = Math.random()*Math.PI/8+Math.PI/6

      for i in [0..obj_count-1]
         dist = Math.random()*@h/8+@h/3
         Ball::create hue,angle,dist,objectRadius,@

         hue += Math.random()*40+60
         hue -= 360 if hue > 360
         angle += Math.random()*Math.PI/4+Math.PI/8

# Figure class for the rectilinear projection
class FigureRect extends Figure
   constructor: (id,w,h) ->

      # call the original constructor
      super id,w,h

      # create the horizontal screen
      @screen =
         x : w/2
         y : h/2-20
         width : w*0.8

      # create the visual screen object
      @screen.vis = @R.path ["M", @screen.x - @screen.width/2, @screen.y, "h", @screen.width]
      @screen.vis.attr(screenAttr).insertBefore(@aboveScreen)

      # populate the figure with balls
      @populate 3

   # project a ball onto our screen using the rectilinear projection
   projectBall: (ball) ->

      # image x coordinates
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

      # call the original constructor
      super id,w,h

      # create the circular screen
      @screen = 
         x : @cam.x # center
         y : @cam.y
         r : 50  # radius
         n : 40 # number of segments

      # To unfold a circular screen, we interpolate a "folding angle"
      # To calculate this angle, we treat the circle to be
      # an n-sided regular polygon.  At each vertex is an angle,
      # which is our "folding angle".  Thus, to unfold, we interpolate
      # this value from the original angle to 180 degrees.
      @screen.foldAngle = Math.PI - 2*Math.PI / @screen.n
      @screen.segLength = Math.sqrt(2*@screen.r*@screen.r*(1-Math.cos(2*Math.PI/@screen.n)))

      # create visual screen circle object
      @screen.vis = @R.path().attr screenAttr
      @screen.vis.insertBefore(@aboveScreen)

      # da is the "delta angle" which is the visual width of the object in degrees
      @da = Math.PI-@screen.foldAngle
      @foldScreen(Math.PI)

      # set our yoyo (empty animation object) to animate our fold angle
      @yoyo.attr(x:1)
      @yoyo.onAnimation => @foldScreen(@screen.foldAngle+@da*@yoyo.attr("x"))

      # populate the figure with colored balls
      @populate 3

      # initialize the scene by calling the event after a ball drag is complete
      # (this clears the viewing cones and unfolds the screen)
      @ballDragEnd()

   # event that is called when a user starts dragging a ball
   # (enables viewing cones and rolls up the screen)
   ballDragStart: ->
      @fadeInCones()
      @yoyo.animate({x:0},screenFoldSpeed, => @foldScreen(@screen.foldAngle))

   # event that is called when a user stops dragging a ball
   # (clears viewing cones and unrolls the screen)
   ballDragEnd: ->
      @fadeOutCones()
      @yoyo.animate({x:1},screenFoldSpeed, => @foldScreen(Math.PI))

   # updates a ball's projection on the circular screen, however folded
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

         # normalize angles (make positive) 
         minAngle += Math.PI*2 if minAngle < 0
         maxAngle += Math.PI*2 if maxAngle < 0
         if maxAngle < minAngle
            [minAngle, maxAngle] = [maxAngle, minAngle]

         # screen is flat
         if @arcAngle < 0.001
            path = [
               "M",
               @screen.x - Math.PI*@screen.r,
               @screen.y - @screen.r,
               "H",
               @screen.x - Math.PI*@screen.r + minAngle*@screen.r,
               "M",
               @screen.x - Math.PI*@screen.r + maxAngle*@screen.r,
               @screen.y - @screen.r,
               "H",
               @screen.x + Math.PI*@screen.r]

         # screen is curved
         else
            # convert angles to the folding arc angles
            minAngle = minAngle / (2*Math.PI) * @arcAngle
            maxAngle = maxAngle / (2*Math.PI) * @arcAngle
            if maxAngle < minAngle
               [minAngle, maxAngle] = [maxAngle, minAngle]

            start = (2*Math.PI - @arcAngle)/2 + Math.PI/2
            path = [
               "M",
               @arcCenterX + @arcRadius * Math.cos(start),
               @arcCenterY + @arcRadius * Math.sin(start),
               "A",
               @arcRadius, @arcRadius, 
               0, # rotation
               0, # large sweep
               1, # arc sweep
               @arcCenterX + @arcRadius * Math.cos(start + minAngle),
               @arcCenterY + @arcRadius * Math.sin(start + minAngle),
               "M",
               @arcCenterX + @arcRadius * Math.cos(start + maxAngle),
               @arcCenterY + @arcRadius * Math.sin(start + maxAngle),
               "A",
               @arcRadius, @arcRadius, 
               0, # rotation
               0, # large sweep
               1, # arc sweep
               @arcCenterX + @arcRadius * Math.cos(start + @arcAngle),
               @arcCenterY + @arcRadius * Math.sin(start + @arcAngle)]

      # projection is contiguous
      else
         # normalize angles (make positive)
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
            # convert angles to the folding arc angles
            minAngle = minAngle / (2*Math.PI) * @arcAngle
            maxAngle = maxAngle / (2*Math.PI) * @arcAngle

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

   # update the screen folding to create a rolling/unrolling effect
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

# Figure class for the stereographic projection
class FigureStereo extends Figure
   constructor: (id,w,h) ->

      # call the original constructor
      super id,w,h

      # circle screen
      @screen1 = 
         x : @cam.x # center
         y : @cam.y
         r : 50  # radius
      @screen1.vis = @R.circle(@screen1.x, @screen1.y, @screen1.r).attr screenAttr
      @screen1.vis.insertBefore(@aboveScreen)

      # stereographic camera
      @cam2 =
         x : @cam.x
         y : @cam.y + @screen1.r
         r : 5

      @cam2.vis = @R.path(camIcon).attr(camAttr)
      @cam2.vis.translate(@cam2.x-16, @cam2.y-10)

      # flat screen
      @screen2 =
         x : w/2
         y : h/2-20
         width: w*0.8
      @screen2.vis = @R.path ["M", @screen2.x - @screen2.width/2, @screen2.y, "h", @screen2.width]
      @screen2.vis.attr(screenAttr).insertBefore(@aboveScreen)

      @populate 2
      @ballDragEnd()

   ballDragStart: ->
      @screen = @screen1
      @cam1.vis.animate({opacity:camOpacityMax},camFadeSpeed)
      @cam2.vis.animate({opacity:camOpacityMin},camFadeSpeed)
      @fadeInCones()
      @projectBalls()

   ballDragEnd: ->
      @screen = @screen2
      @cam1.vis.animate({opacity:camOpacityMin},camFadeSpeed)
      @cam2.vis.animate({opacity:camOpacityMax},camFadeSpeed)
      @fadeInCones()
      @projectBalls()

   projectBall: (ball) ->

      # circle projection points
      cx1 = @screen1.x + @screen1.r * Math.cos(ball.angle-ball.da)
      cy1 = @screen1.y + @screen1.r * Math.sin(ball.angle-ball.da)
      cx2 = @screen1.x + @screen1.r * Math.cos(ball.angle+ball.da)
      cy2 = @screen1.y + @screen1.r * Math.sin(ball.angle+ball.da)

      # flat projection points
      ix1 = (cx1-@cam2.x) / (cy1-@cam2.y) * (@screen2.y-@cam2.y) + @cam2.x
      ix2 = (cx2-@cam2.x) / (cy2-@cam2.y) * (@screen2.y-@cam2.y) + @cam2.x

      # bound to screen
      ix1b = bound ix1, @screen2.x - @screen2.width/2, @screen2.x+@screen2.width/2
      ix2b = bound ix2, @screen2.x - @screen2.width/2, @screen2.x+@screen2.width/2

      ball.image.attr path:[
         # panorama projection
         "M",cx1,cy1,"A",
         @screen1.r, @screen1.r,
         0, # rotation
         0, # large sweep
         1, # arc sweep
         cx2,cy2,
         # rectilinear projection
         "M",ix1b,@screen2.y,"H",ix2b
         ]

      if @screen == @screen1
         ball.cone.attr path:[
            "M", @cam1.x, @cam1.y,
            "L", ball.cx1, ball.cy1,
            "L", ball.cx2, ball.cy2,
            "Z"]
      else
         ball.cone.attr path:[
            "M", @cam2.x, @cam2.y,
            "L", ix1, @screen2.y,
            "L", ix2, @screen2.y,
            "Z"]



# a ball to be projected onto a screen
class Ball
   constructor: (@x,@y,@r,@color,@figure) ->

      @circle = @figure.R.circle(x,y,r).attr fill:color, stroke:"none"
      @image = @figure.R.path().attr "stroke-width":imageThickness, stroke:@color
      @cone = @figure.R.path().attr fill:@color, opacity:coneOpacity, stroke:"none"
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
         @figure.ballDragStart() if @figure.ballDragStart?

      touchDragEnd = =>
         @figure.ballDragEnd() if @figure.ballDragEnd?

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

# create the figures
window.onload = ->
   new FigureRect("figure1", 650, 300)
   new FigureCircle("figure2", 650, 300)
   new FigureStereo("figure3", 650, 300)
