g++ -o ywm \
	-lxcb -lxcb-icccm -lxcb-ewmh -lxcb-xtest ywm.cpp && \
g++ -o y_move -lxcb y_move.cpp && \
g++ -o y_resize -lxcb y_resize.cpp
