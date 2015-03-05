#!/bin/bash

set -e

# Detect OS
# from: http://stackoverflow.com/a/8597411/142317
if   [[ "$OSTYPE" == "linux-gnu" ]]; then OS="unix"
elif [[ "$OSTYPE" == "darwin"* ]];   then OS="unix"
elif [[ "$OSTYPE" == "cygwin" ]];    then OS="win"
elif [[ "$OSTYPE" == "msys" ]];      then OS="win"
elif [[ "$OSTYPE" == "win32" ]];     then OS="win"
elif [[ "$OSTYPE" == "freebsd"* ]];  then OS="unix"
else
  OS="unix"
  echo "Unable to detect OS, assuming unix..."
fi

# Configure OS vars
if [[ "$OS" == "unix" ]]; then
  EXT=
  QUAKE_DIR=~/.tyrquake
else
  EXT=.exe
  QUAKE_DIR=game
fi

echo
echo "Building engine..."
pushd engine
make bin/tyr-quake$EXT
popd

echo
echo "Copying engine to game directory..."
mkdir -p game
rm -f game/blinky$EXT
cp engine/bin/tyr-quake$EXT game/blinky$EXT

echo
echo "Copying lua scripts to game directory..."
mkdir -p $QUAKE_DIR
cp -r lua-scripts/* $QUAKE_DIR

echo
echo "Adding default config..."
CFG="$QUAKE_DIR/id1/config.cfg"
if [ ! -f $CFG ]; then
  mkdir -p $(dirname $CFG)
  touch $CFG
  # add modern movement keys
  echo "bind w +forward" >> $CFG
  echo "bind s +back" >> $CFG
  echo "bind a +moveleft" >> $CFG
  echo "bind d +moveright" >> $CFG
  # and some helpful shortcuts
  echo "bind 0 f_shortcutkeys" >> $CFG
  echo "bind r f_rubix" >> $CFG
fi

if [[ "$OS" == "win" ]]; then
  echo
  echo "Making sure windows README is ready..."
  pushd game
  rm -f README.md
  if [[ -f README-windows.txt ]]; then
    mv README-windows.txt README.txt
  fi
  popd
fi

echo
echo -e "\033[32mSuccessfully built!\033[0m"
echo -e "  Run \033[36mplay.sh\033[0m to starting playing!"
echo
