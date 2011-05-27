#!/usr/bin/python

import sys
import os
import numpy
import operator

# use python colormath library (requires numpy)
from colormath.color_objects import *

if len(sys.argv) < 5:
   sys.exit('Usage: %s r g b a (where 0 <= r,g,b,a <= 255)' % sys.argv[0])

if not os.path.exists('palette'):
   sys.exit('ERROR: \'palette\' not found')

# filter r,g,b,a
fr = int(sys.argv[1])
fg = int(sys.argv[2])
fb = int(sys.argv[3])
fa = int(sys.argv[4])/255.0

# dump Quake palette with "dumppal" command (produces a 'palette' file)
# load it into Python with this list comprehension
pal = [[int(val) for val in line.split(',') if val.strip()] for line in open('palette').readlines()]

for i in xrange(256):
   r = pal[i][0]
   g = pal[i][1]
   b = pal[i][2]

   # get target color by blending
   r = numpy.clip(fa*fr + (1-fa)*r, 0, 255)
   g = numpy.clip(fa*fg + (1-fa)*g, 0, 255)
   b = numpy.clip(fa*fb + (1-fa)*b, 0, 255)
   color = RGBColor(r,g,b)

   # compute distance to the target color from all the colors on the palette
   paldist = [color.delta_e(RGBColor(*p), mode='cmc', pl=1, pc=1) for p in pal]
   
   # print out the palette index of the minimum distance
   closestindex = paldist.index(min(paldist))
   print("%d," % closestindex),
