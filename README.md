blinky
======

Blinky is a fisheye mod for Quake, based on Fisheye Quake.  Its purpose is to explore several types of panoramic projections defined by user-generated Lua scripts.

See official website for details and media: 
http://shaunew.github.com/blinky


Compiling and Installation:
---------------------------

Linux:
1. Install LuaJIT
2. build tyr-quake:

    make prepare
    make tyr-quake

3. copy tyr-quake to your Quake directory
4. copy globes/ and lenses/ folder to $HOME/.tyrquake


Windows:
1. build tyr-quake.exe:

    make prepare
    make tyr-quake.exe

2. copy everything from the installation binary at the link above to your Quake directory, along with the freshly built tyr-quake.exe
