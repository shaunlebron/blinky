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

#import "QuakeArguments.h"
#import "QuakeArgument.h"

@implementation QuakeArguments

- (id)init {
    self = [super init];
    if (!self)
        return nil;
    
    quakeArgs = [[NSMutableArray alloc] init];
    return self;
}

- (id)initWithArguments:(char **)argv count:(int)argc {
    int i;
    NSString *next;
    NSString *current;
    QuakeArgument *argument;

    self = [self init];
    if (!self)
        return nil;
    
    if (argc > 0) {
        for (i = 0; argv[i]; i++) {
            current = [NSString stringWithCString:argv[i] encoding:NSASCIIStringEncoding];
            if (i < argc-1) {
                next = [NSString stringWithCString:argv[i+1] encoding:NSASCIIStringEncoding];
            } else {
                next = nil;
            }
        
            if (next != nil && [next characterAtIndex:0] != '-' && [next characterAtIndex:0]  != '+') {
                argument = [[QuakeArgument alloc] initWithArgument:current andValue:next];
                i++;
            } else {
                argument = [[QuakeArgument alloc] initWithArgument:current];
            }
            [quakeArgs addObject:argument];
            [argument release];
        }
    }
    
    return self;
}

- (void)parseArguments:(NSString *)args {
    int i;
    unichar c;
    unichar p = ' ';
    NSMutableString *word = nil;
    NSMutableArray *words = [[NSMutableArray alloc] init];
    BOOL quoted = FALSE;

    [quakeArgs removeAllObjects];
    
    for (i = 0; i < [args length]; i++) {
    
        c = [args characterAtIndex:i];
        switch (c) {
            case ' ':
                if (!quoted) {
                    // ignore whitespace
                    if (p == ' ')
                        break;
                    
                    if (word != nil) {
                        [words addObject:word];
                        [word release];

                        word = nil;
                    }
                }
                break;
            case '"':
                quoted = !quoted;
                break;
            default:
                if (p == ' ') {
                    word = [[NSMutableString alloc] init];
                }
                [word appendFormat:@"%C", c];
                break;
        }
        p = c;
    }
    
    if (word != nil) {
        [words addObject:word];
        [word release];
    }
    
    NSString *current;
    NSString *next;
    QuakeArgument *argument = nil;

    for (i = 0; i < [words count];) {
        current = [words objectAtIndex:i++];
        if (i < [words count]) {
            next = [words objectAtIndex:i++];
            unichar c = [next characterAtIndex:0];
            if (c != '-' && c != '+')
                argument = [[QuakeArgument alloc] initWithArgument:current andValue:next];
            else
                i--;
            
        }
        
        if (argument == nil) {
            argument = [[QuakeArgument alloc] initWithArgument:current];
        }
        
        [quakeArgs addObject:argument];
        [argument release];
        argument = nil;
    }
}

- (void)addArgument:(NSString *)name {
    QuakeArgument *argument = [[QuakeArgument alloc] initWithArgument:name];
    [quakeArgs addObject:argument];
    
    [argument release];
}

- (void)addArgument:(NSString *)name withValue:(NSString *)value {
    QuakeArgument *argument = [[QuakeArgument alloc] initWithArgument:name andValue:value];
    [quakeArgs addObject:argument];
    
    [argument release];
}

- (void)removeArgument:(NSString *)arg {
	[quakeArgs removeObject:arg];
}

- (QuakeArgument *)argument:(NSString *)name {
    NSEnumerator *enumerator = [quakeArgs objectEnumerator];
    QuakeArgument *argument;

    while ((argument = [enumerator nextObject])) {
        if ([name isEqualToString:[argument name]])
            return argument;
    }
    
    return nil;
}

- (int)count {
    int c = 0;

    NSEnumerator *enumerator = [quakeArgs objectEnumerator];
    QuakeArgument *argument;
    
    while ((argument = [enumerator nextObject])) {
        c++;
        if ([argument hasValue])
            c++;
    }
    
    return c;
}

- (void)setArguments:(char **)args {
    int i = 0;
    
    NSEnumerator *enumerator = [quakeArgs objectEnumerator];
    QuakeArgument *argument;
    
    while ((argument = [enumerator nextObject])) {
        args[i++] = (char *)[[argument name] cStringUsingEncoding:NSASCIIStringEncoding];

        if ([argument hasValue])
            args[i++] = (char *)[[argument value] cStringUsingEncoding:NSASCIIStringEncoding];
    }
}

- (NSString *)description {
    int i;
    NSMutableString *buffer = [[NSMutableString alloc] init];

    for (i = 0; i < [quakeArgs count]; i++) {
        if (i > 0)
            [buffer appendString:@" "];
        
        QuakeArgument *argument = [quakeArgs objectAtIndex:i];
        [buffer appendString:[argument name]];
        
        if ([argument hasValue]) {
            [buffer appendString:@" "];
            [buffer appendString:[argument value]];
        }
    }
    
    return buffer;
}

- (void) dealloc {
    [quakeArgs release];
    [super dealloc];
}


@end
