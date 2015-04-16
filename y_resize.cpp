// window: resize, following pointer
#include <xcb/xcb.h>
#include <unistd.h>
#include <stdlib.h> // atoi

int main(int argc, char **argv, char **envp) {
	xcb_connection_t *conn; // xcb connection
	xcb_screen_t *screen; // xcb screen
	xcb_drawable_t rootwin, win; // window being operated upon
	xcb_query_pointer_reply_t *pointer; // pointer position and stuff
	xcb_get_geometry_reply_t *geom; // window's geometry request structure
	int16_t offset[2]; // pointer's offset within window
	int16_t oldpos[2]; // previous pointer position
	int16_t origpos[2]; // initial pointer position
	int16_t origwpos[2]; // initial window position
	int16_t origwsize[2]; // initial window size
	uint32_t values[4]; // used for calls to xcb_configure_window
	int8_t top=0, right=0, bottom=0, left=0; // which side(s) are we moving

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
	geom = xcb_get_geometry_reply(conn, xcb_get_geometry(conn, win), 0);
	offset[0] = pointer->root_x - geom->x;
	offset[1] = pointer->root_y - geom->y;
	origpos[0] = oldpos[0];
	origpos[1] = oldpos[1];
	origwpos[0] = geom->x;
	origwpos[1] = geom->y;
	origwsize[0] = geom->width;
	origwsize[1] = geom->height;

/*
	//  ___________    We split window in 9 quadrants, and depending on
	// |___|___|___|   which quad the mouse pointer is, we move either
	// |___|_+_|___|   one or two sides, or if in the center - all sides
	// |___|___|___|   will move in opposite directions.
	//
	//   0   1   2	   We divide width of window into 3, and then divide
	//   4   5   6     pointer's horisontal offset by that amount to get
	//   8   9   10    number 0, 1 or 2. We do the same with vertical
	//                 offset, but we multiply resulting number by 4 and
	// sum them up to get a single number corresponding to a quadrant.

	int16_t wb3 = geom->width / 3; // one third of window's width
	uint8_t quad = offset[0] / wb3; // which quad is pointer in?
	int16_t hb3 = geom->height / 3;
	quad += (offset[1] / hb3) * 4;

	switch(quad) {
	case 0: top = 1; left = 1; break;
	case 1: top = 1; break;
	case 2: top = 1; right = 1; break;
	case 4: left = 1; break;
	case 5: top = 1; right = 1; bottom = -1; left = -1; break;
	case 6: right = 1; break;
	case 8: left = 1; bottom = 1; break;
	case 9: bottom = 1; break;
	case 10: right = 1; bottom = 1; break;
	}
*/
	top = 1; right = 1; bottom = -1; left = -1;

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

		// adjust new position and size of the window
		values[0] = int16_t(origwpos[0] +
				(pointer->root_x - origpos[0]) * left);
		values[1] = int16_t(origwpos[1] +
				(pointer->root_y - origpos[1]) * top);
		int16_t checkres = int16_t(origwsize[0] +
				(pointer->root_x - origpos[0]) * right +
				(pointer->root_x - origpos[0]) * -left);
		if(checkres < 1) {
			continue;
		}
		values[2] = checkres;
		checkres = int16_t(origwsize[1] +
				(pointer->root_y - origpos[1]) * -top +
				(pointer->root_y - origpos[1]) * bottom);
		if(checkres < 1) {
			continue;
		}
		values[3] = checkres;
		xcb_configure_window(conn, win,
				XCB_CONFIG_WINDOW_X |
				XCB_CONFIG_WINDOW_Y |
				XCB_CONFIG_WINDOW_WIDTH |
				XCB_CONFIG_WINDOW_HEIGHT, values);
		xcb_flush(conn);
	}
	return 0;
}
