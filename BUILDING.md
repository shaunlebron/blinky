## Windows

1. Get [MinGW installer](http://sourceforge.net/projects/mingw/files/Installer/mingw-get-setup.exe/download), and install the following with the package manager:
  - gcc
  - lua
  - sdl2
1. Install msys
1. Add mingw/bin and msys/bin to system path
1. Open msys/bin/msys to open command line

```sh
./build.sh
./play.sh
```

## Mac OS X

- Install [Command Line Tools for XCode](https://developer.apple.com/downloads/) (you need an apple developer account, free)
- Install Lua 5.2

  ```sh
  curl -R -O http://www.lua.org/ftp/lua-5.2.0.tar.gz
  tar zxf lua-5.2.0.tar.gz
  cd lua-5.2.0
  make macosx test
  sudo make install
  ```

- Install SDL2
  - [Download](https://www.libsdl.org/release/SDL2-2.0.3.dmg)
  - Copy SDL2.Framework to /Library/Frameworks/

```sh
./build.sh
./play.sh
```

## Linux

- Install Lua 5.2

  ```sh
  curl -R -O http://www.lua.org/ftp/lua-5.2.0.tar.gz
  tar zxf lua-5.2.0.tar.gz
  cd lua-5.2.0
  make linux test
  sudo make install
  ```

- Install SDL 2

  ```sh
  sudo apt-get install libsdl2-dev
  ```

- Install Xxf86dga

  ```sh
  sudo apt-get install libxxf86dga-dev
  ```

```sh
./build.sh
./play.sh
```
