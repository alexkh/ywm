// window: move following pointer
#include <xcb/xcb.h>
#include <unistd.h>
#include <stdlib.h> // atoi

int main(int argc, char **argv, char **envp) {
	xcb_connection_t *conn; // xcb connection
	xcb_screen_t *screen; // xcb screen
	xcb_drawable_t rootwin, win; // window being operated upon
	xcb_query_pointer_reply_t *pointer; // pointer position and stuff
	xcb_get_geometry_reply_t *geom; // window's geometry request structure
	int16_t offset[2]; // pointer's offset within window = const
	int16_t oldpos[2]; // previous pointer position
	uint32_t values[2]; // used for calls to xcb_configure_window

	// connect and get root window
	conn = xcb_connect(NULL, NULL);
	if(xcb_connection_has_error(conn)) return 1;
	screen = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;
	rootwin = screen->root;

	// get initial window and offset
	pointer = xcb_query_pointer_reply(conn,
				xcb_query_pointer(conn, rootwin), 0);
	if(argc > 1) {
		win = atoi(argv[1]);
	} else {
		win = pointer->child;
	}
	oldpos[0] = pointer->root_x;
	oldpos[1] = pointer->root_y;

	// we also need to calculate the offset from the window's position
	geom = xcb_get_geometry_reply(conn,
		xcb_get_geometry(conn, win), 0);
	offset[0] = pointer->root_x - geom->x;
	offset[1] = pointer->root_y - geom->y;

	while(1) {
		usleep(50000); // sleep 50 milliseconds
		pointer = xcb_query_pointer_reply(conn,
			xcb_query_pointer(conn, rootwin), 0);
		if(oldpos[0] == pointer->root_x &&
					oldpos[1] == pointer->root_y) {
			continue;
		}
		oldpos[0] = pointer->root_x;
		oldpos[1] = pointer->root_y;

		values[0] = int16_t(pointer->root_x - offset[0]);
		values[1] = int16_t(pointer->root_y - offset[1]);
		xcb_configure_window(conn, win,
				XCB_CONFIG_WINDOW_X |
				XCB_CONFIG_WINDOW_Y, values);
		xcb_flush(conn);
	}
	return 0;
}
