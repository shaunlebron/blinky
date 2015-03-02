#!/bin/bash

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

cd play

if [[ "$OS" == "unix" ]]; then
  ./blinky
else
  # Just open the play folder in explorer.
  # Running it from msys terminal makes it load the config from unix home directory instead of the game folder.
  # So we just expect the user to double-click "blinky.exe" in the explorer.
  explorer .
fi
