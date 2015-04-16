g++ -o ywm \
	-lxcb -lxcb-icccm -lxcb-ewmh -lxcb-xtest ywm.cpp && \
g++ -o y_move -lX11 y_move.cpp && \
g++ -o y_resize -lX11 y_resize.cpp
