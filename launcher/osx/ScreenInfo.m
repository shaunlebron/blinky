/*
Copyright (C) 2007-2008 Kristian Duske

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#import "ScreenInfo.h"


@implementation ScreenInfo

- (id)initWithWidth:(int)w height:(int)h bpp:(int)b {

    self = [super init];
    if (!self)
        return nil;
    
    width = w;
    height = h;
    bpp = b;
    
    description = [NSString stringWithFormat:@"%d x %d %d Bit", width, height, bpp];
    
    return self;
}

- (int)width {

    return width;
}

- (int)height {

    return height;
}

- (int)bpp {
    
    return bpp;
}

- (NSString *)description {

    return description;
}

@end
