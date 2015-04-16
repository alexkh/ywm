# ywm
Minimalist window manager with only dependence: libXCB

At this point, a mouse with at least 5 buttons is required, the buttons 4 and 5,
normally acting as 'back' and 'forward' buttons in browsers. This window manager
assigns a different function to the back button: it is used for moving windows.
See Usage section for details.

Download using GIT:
-------------------

git clone https://github.com/alexkh/ywm.git



Build + Install
---------------

chmod 700 install mk

cd ywm

./mk

sudo ./install



Usage
-----

Most of the operations are performed using 'back' mouse button:

'mouse-back-button' + move mouse:		to move a window

'mouse-back-button' + 'mouse-right-button':	to resize window

'mouse-back-button' + 'mouse-middle-button':	full screen on/off

'mouse-back-button' + 'mouse-wheel-down':	to close a window

'mouse-back-button' + 'mouse-wheel-up':		to kill -9 the program


Three xterm teminals are started automatically, but you can add more by
pressing 'Mod-key' + 'Enter'


