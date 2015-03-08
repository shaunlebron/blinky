# Blinky

<img src="readme-img/blinky-rocket.png" align="right" width="220px"/>

Proof of concept to __break the 180º Field of View limit__ in games.
Modifies Quake to use fisheyes, panoramas, and cartographic projections.

[>> Watch a demo video](http://youtu.be/jQOJ3yCK8pI)

<img src="readme-img/windows.png" height="16px"> __[Download for Windows](https://github.com/shaunlebron/blinky/releases/download/1.3/blinky-1.3-windows.zip)__  
<img src="readme-img/apple.png"   height="16px"> __[Download for Mac](https://github.com/shaunlebron/blinky/releases/download/1.3/blinky-1.3-mac.zip)__  
<img src="readme-img/linux.png"   height="16px"> (Help with linux, [issue #74](https://github.com/shaunlebron/blinky/issues/74))

### Overview

Standard projections are not intended for wide-angle viewing.  The [Panini]
projection seems to be the best way to represent your natural 180º field of
view.

![old-and-new](readme-img/old-and-new.jpg)

To use non-standard projections, Blinky first snaps multiple pictures around
you to form a __Globe__ of pixels.  Then it projects all those pixels to the
screen using a __Lens__.  Finally, it uses a __Rubix__ grid if you wish to
visualize the mapping.

![map](readme-img/map.gif)

Blinky has shortcut keys for trying several Globes and Lenses.  If you want
more control, use the [console commands](#console-commands) or [edit/create
your own globes and lenses](#lua-scripts).

![keys](readme-img/keys.png)

Blinky has many fisheye and panoramic lenses from photography, but it also has
many map projections from cartography that will give you a 360º view.

![worlds](readme-img/worlds.jpg)

## Technical Details

Blinky is a modification of the famous [Fisheye Quake].

- adds a Lua scripting environment for defining:
  - _Globes_ (for capturing the environment)
  - _Lenses_ (for projecting a wide-angle image)
- uses the cross-platform [TyrQuake] engine for Windows, Mac, and Linux

### Building from source

After some [manual setup](BUILDING.md) for your OS, you can build/play with:

```
$ ./build.sh
$ ./play.sh
```

### Console Commands

Press `~` to access the command console.  Use the `Tab` key for help completing
a partial command.

```sh
fisheye <0|1>     # enable/disable fisheye mode
f_help            # show quick start options
f_globe <name>    # choose a globe (affects picture quality and render speed)
f_lens <name>     # choose a lens (affects the shape of your view)

f_fov <degrees>   # zoom to a horizontal FOV
f_vfov <degrees>  # zoom to a vertical FOV
f_cover           # zoom in until screen is covered (some parts may be hidden)
f_contain         # zoom out until screen contains the entire image (if possible)

f_rubix           # display colored grid for each rendered view in the globe
f_saveglobe       # take screenshots of each globe face (environment map)
```

### Lua Scripts

To create/edit globes and lenses, check out the following guides:

- [Create a Globe](game/lua-scripts/globes)
- [Create a Lens](game/lua-scripts/lenses)

### Engine Code

- [engine/NQ/fisheye.c](engine/NQ/fisheye.c) - new engine code
- [engine patch](engine/fisheye.patch) - engine modifications

## Future

I hope to apply this to modern graphics using frame buffers for
environment-capturing and pixel shaders for projection.  It would be
interesting to see its impact on performance.

If this modern method is performant enough, I think Panini/Stereographic could
easily become a standard for gamers demanding wide-angle video.  But if it is
not performant enough for live applications, I think it could still prove
useful in post-processed videos using something like [WolfCam].  For example,
spectators could benefit from wide-angle viewings of previously recorded
competitive matches or even [artistic montages].

## Thanks

This project would not exist without these people!

- __Wouter van Oortmerssen__ for creating & open-sourcing [Fisheye Quake]
- __Peter Weiden__ for creating [fisheye diagrams] on Wikipedia
- __Kevin Shanahan__ for creating/maintaining a cross-platform Quake engine, [TyrQuake]

## License

Copyright © 2011-2015 Shaun Williams

The MIT License



[Fisheye Quake]:http://strlen.com/gfxengine/fisheyequake/
[TyrQuake]:http://disenchant.net/tyrquake/
[Panini]: http://tksharpless.net/vedutismo/Pannini/
[Quincuncial]:http://en.wikipedia.org/wiki/Peirce_quincuncial_projection
[artistic montages]:http://youtu.be/-T6IAHWMd2I
[WolfCam]:http://www.wolfcamql.fr/en
[fisheye diagrams]:http://en.wikipedia.org/wiki/Fisheye_lens#Mapping_function
