#!/bin/bash

echo "#define REDPAL \\"
echo `./shiftpal.py 255 000 000 42`

echo "#define BLUEPAL \\"
echo `./shiftpal.py 000 000 255 42`

echo "#define YELLOWPAL \\"
echo `./shiftpal.py 255 255 000 42`

echo "#define CYANPAL \\"
echo `./shiftpal.py 000 255 255 42`

echo "#define MAGENTAPAL \\"
echo `./shiftpal.py 255 000 255 42`

echo "#define WHITEPAL \\"
echo `./shiftpal.py 255 255 255 42`
