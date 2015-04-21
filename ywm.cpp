#include <xcb/xcb.h>
#include <xcb/xcb_atom.h>
#include <xcb/xcb_ewmh.h>
#include <xcb/xcb_icccm.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h> // exit
#include <stdio.h>

#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

#include "vec.hpp"

#include <iostream>
#include <fstream>
#include <map>
using namespace std;

int utf8toXChar2b(xcb_char2b_t *output_r, int outsize, const char *input,
								int inlen) {
	int j, k;
	for(j =0, k=0; j < inlen && k < outsize; j ++) {
		unsigned char c = input[j];
		if(c < 128) {
			output_r[k].byte1 = 0;
			output_r[k].byte2 = c;
			k++;
		} else if(c < 0xC0) {
			/* we're inside a character we don't know  */
			continue;
		} else switch(c&0xF0) {
		case 0xC0: case 0xD0: /* two bytes 5+6 = 11 bits */
			if(inlen < j + 1) { return k; }
			output_r[k].byte1 = (c&0x1C) >> 2;
			j++;
			output_r[k].byte2 = ((c&0x3) << 6) + (input[j]&0x3F);
			k++;
			break;
		case 0xE0: /* three bytes 4+6+6 = 16 bits */
			if(inlen < j + 2) { return k; }
			j++;
			output_r[k].byte1 = ((c&0xF) << 4) + ((input[j]&0x3C)
									>> 2);
			c = input[j];
			j++;
			output_r[k].byte2 = ((c&0x3) << 6) + (input[j]&0x3F);
			k++;
			break;
		case 0xFF:
			/* the character uses more than 16 bits */
			continue;
		}
	}
	return k;
}

// a small window's info structure that will be stored in a hashtable
struct Wdata {
	uint64_t flag; // 1=override redirect, 2=fullscreen
	xcb_window_t window; // window
	xcb_window_t parent; // parent window
	uint16_t x, y, w, h; // coordinates and size
};

class Wm {
public:
	char **envp; // environment variables
	xcb_atom_t wm_protocols; // WM_PROTOCOLS atom
	xcb_atom_t wm_delete_window; // WM_DELETE_WINDOW atom
	void init(); // set up communication with the X server
	void draw(); // handle refreshing of drawable areas
	void event_loop(); // main event loop
	xcb_atom_t getatom(char *atom_name);
private:
	xcb_atom_t _NET_WM_NAME;
	static const uint8_t OP_NORMAL = 0;
	static const uint8_t OP_MOVE = 1;
	static const uint8_t OP_RESIZE = 2;
	static const uint8_t OP_AUX = 4;
	map<int, Wdata>wdata; // a map of all windows referenced by id
	uint8_t opmode; // 0=normal, 1=moving, 2=resizing
	int32_t mainscreen; // used during connection
	char *dispname; // name of display taken from env DISPLAY variable
	ofstream log; // log file
	xcb_connection_t *conn; // xcb connection
	xcb_ewmh_connection_t ewconn;
	xcb_screen_t *screen; // xcb screen
	xcb_drawable_t rootwin; // root window
	xcb_drawable_t focuswin; // track input focus
	xcb_drawable_t win; // a child window being acted upon
	char winstr[20]; // the child iwndow's id converted to string
	xcb_get_geometry_reply_t *geom; // geometry of a window
	xcb_gcontext_t fg, bg; // basic colors
	xcb_gcontext_t mono1; // fixed-width font
	xcb_gcontext_t sans1; // sans-serif font
	xcb_gcontext_t serif1; // serif font
	xcb_generic_error_t *error = NULL; // error from xcb if any
	pid_t child_pid; // used when starting a subprocess

	void check_cookie(xcb_void_cookie_t cookie, const char *err_msg);
	struct TextItem; // used only inside draw_text function
	void draw_text(xcb_gcontext_t fontgc, int16_t x, int16_t y,
							const char *label);
	char status[1024]; // debug status displayed in top left corner
	char lastev[1024]; // last event received
	xcb_gcontext_t get_font_gc(const char *font_name);
	void set_cursor(xcb_screen_t *screen, xcb_window_t window, int cur_id);
	void enter_move(); // enter move mode (opmode = 1)
	void enter_resize(); // enter resize mode (opmode = 2)
	void print_status(const char *); // debug status message
	size_t get_window_name(xcb_window_t win, char *buf, size_t len);
	uint16_t offset_x, offset_y; // used when stacking windows
};

xcb_atom_t Wm::getatom(char *atom_name) {
	xcb_intern_atom_cookie_t atom_cookie;
	xcb_atom_t atom;
	xcb_intern_atom_reply_t *rep;

	atom_cookie = xcb_intern_atom(conn, 0, strlen(atom_name), atom_name);
	rep = xcb_intern_atom_reply(conn, atom_cookie, NULL);
	if(rep != NULL) {
		atom = rep->atom;
		free(rep);
		return atom;
	}
	return 0;
}

void Wm::check_cookie(xcb_void_cookie_t cookie, const char *err_msg) {
	error = xcb_request_check(conn, cookie);
	if(error) {
		xcb_disconnect(conn);
		exit(-1);
	}
}

struct Wm::TextItem {
	uint8_t nchars;
	int8_t	delta;
	xcb_char2b_t text[257]; // 256 2byte characters
};

void Wm::draw_text(xcb_gcontext_t fontgc, int16_t x, int16_t y,
							const char *label) {
	TextItem ti;
	ti.nchars = utf8toXChar2b(ti.text, 256, label, strlen(label));
	ti.delta = 0;
	xcb_void_cookie_t text_cookie = xcb_poly_text_16_checked(conn,
		rootwin, fontgc, x, y, ti.nchars * 2 + 2, (const uint8_t*)&ti);
	check_cookie(text_cookie, "can't draw text");
}

xcb_gcontext_t Wm::get_font_gc(const char *font_name) {
	// get font context
	xcb_font_t font = xcb_generate_id(conn);
	xcb_void_cookie_t font_cookie = xcb_open_font_checked(conn,
			font, strlen(font_name), font_name);
	check_cookie(font_cookie, "can't open font");
	// create graphics context
	xcb_gcontext_t fontgc = xcb_generate_id(conn);
	{
		uint32_t mask = XCB_GC_FOREGROUND | XCB_GC_BACKGROUND |
				XCB_GC_FONT;
		uint32_t value_list[3] = {screen->white_pixel,
			screen->black_pixel, font};
		xcb_void_cookie_t gc_cookie = xcb_create_gc_checked(conn,
			fontgc, rootwin, mask, value_list);
		check_cookie(gc_cookie, "can't create font gc");
	}
	// close font
	font_cookie = xcb_close_font_checked(conn, font);
	check_cookie(font_cookie, "Can't close font");
	return fontgc;
}

void Wm::set_cursor(xcb_screen_t *screen, xcb_window_t window, int cur_id) {
	xcb_font_t font = xcb_generate_id(conn);
	xcb_void_cookie_t font_cookie = xcb_open_font_checked(
				conn, font, strlen("cursor"), "cursor");
	check_cookie(font_cookie, "can't open font");
	xcb_cursor_t cursor = xcb_generate_id(conn);
	xcb_create_glyph_cursor(conn, cursor, font, font, cur_id, cur_id + 1,
		0, 0, 0, 52428, 52428, 26214);
	xcb_gcontext_t gc = xcb_generate_id(conn);
	uint32_t mask = XCB_GC_FOREGROUND | XCB_GC_BACKGROUND | XCB_GC_FONT;
	uint32_t values_list[3];
	values_list[0] = screen->white_pixel;
	values_list[1] = screen->black_pixel;
	values_list[2] = font;
	xcb_void_cookie_t gc_cookie = xcb_create_gc_checked(
				conn, gc, window, mask, values_list);
	check_cookie(gc_cookie, "Can't create gc");
	mask = XCB_CW_CURSOR;
	uint32_t value_list = cursor;
	xcb_change_window_attributes(conn, window, mask, &value_list);
	xcb_free_cursor(conn, cursor);
	xcb_close_font(conn, font);
}

void Wm::init() {
	offset_x = 60; // initialize window stacking offsets
	offset_y = 20;
	// set up logging
	dispname = getenv("DISPLAY");
	string logfname = "/tmp/wm";
	logfname.append(dispname);
	log.open(logfname.c_str());
	if(!log) {
		exit(-2);
	}
	log << "Starting ywm\n" << flush;
	// connect and get the root window
	conn = xcb_connect(NULL, &mainscreen);
	if(xcb_connection_has_error(conn)) exit(1);
	screen = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;
	rootwin = screen->root;
	wm_protocols = getatom((char *)&"WM_PROTOCOLS");
	wm_delete_window = getatom((char *)&"WM_DELETE_WINDOW");

	xcb_intern_atom_cookie_t *ewmhcookie =
					xcb_ewmh_init_atoms(conn, &ewconn);
	if(!xcb_ewmh_init_atoms_replies(&ewconn,
					ewmhcookie, NULL)) exit(-2);

	// define foreground and background colors
	fg = xcb_generate_id(conn);
	{
		uint32_t mask = XCB_GC_FOREGROUND |
						XCB_GC_LINE_STYLE;
		uint32_t values[2] = {screen->white_pixel, 0};
		xcb_create_gc(conn, fg, rootwin, mask, values);
	}
	bg = xcb_generate_id(conn);
	{
		uint32_t mask = XCB_GC_FOREGROUND |
						XCB_GC_FILL_STYLE;
		uint32_t values[2] = {screen->black_pixel, 0};
		xcb_create_gc(conn, bg, rootwin, mask, values);
	}

	// create fonts
	mono1 = get_font_gc("-*-fixed-bold-r-*-*-13-*-*-*-*-*-ISO10646-1");
	sans1 = get_font_gc("-*-fixed-medium-r-*-*-7-*-*-*-*-*-ISO10646-1");
//	serif1 = get_font_gc("-*-times-medium-r-*-*-14-*-*-*-*-*-*-*");

//	draw();
	uint32_t evmask = XCB_EVENT_MASK_BUTTON_PRESS |
		XCB_EVENT_MASK_BUTTON_RELEASE |
		XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_PRESS |
		XCB_EVENT_MASK_ENTER_WINDOW |
		XCB_EVENT_MASK_LEAVE_WINDOW |
//		XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
		XCB_EVENT_MASK_STRUCTURE_NOTIFY |
		XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
		XCB_EVENT_MASK_PROPERTY_CHANGE |
		XCB_EVENT_MASK_VISIBILITY_CHANGE |
		XCB_EVENT_MASK_EXPOSURE;
	xcb_change_window_attributes(conn, rootwin, XCB_CW_EVENT_MASK, &evmask);
	// set default cursor
	set_cursor(screen, rootwin, 68);
//	xcb_flush(conn);
	// button 8 is used for moving windows
	xcb_grab_button(conn, 0, rootwin, XCB_EVENT_MASK_BUTTON_PRESS |
		XCB_EVENT_MASK_BUTTON_RELEASE, XCB_GRAB_MODE_ASYNC,
		XCB_GRAB_MODE_ASYNC, rootwin, XCB_NONE, 9, XCB_MOD_MASK_ANY);

	// button 9 is used for entering auxillary mode
	xcb_grab_button(conn, 0, rootwin, XCB_EVENT_MASK_BUTTON_PRESS |
		XCB_EVENT_MASK_BUTTON_RELEASE, XCB_GRAB_MODE_ASYNC,
		XCB_GRAB_MODE_ASYNC, rootwin, XCB_NONE, 8, XCB_MOD_MASK_ANY);

	// bind Mod4+Enter for starting xterm
	xcb_grab_key(conn, 0, rootwin, XCB_MOD_MASK_2 | XCB_MOD_MASK_4, 36,
		XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
	xcb_grab_key(conn, 0, rootwin, XCB_MOD_MASK_4, 36,
		XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);

	// bind Ctrl+Alt+Backspace for exiting X11
	xcb_grab_key(conn, 0, rootwin, XCB_MOD_MASK_2 | XCB_MOD_MASK_CONTROL |
				XCB_MOD_MASK_1, 22,
		XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
	xcb_grab_key(conn, 0, rootwin, XCB_MOD_MASK_CONTROL |
					XCB_MOD_MASK_1, 22,
		XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);

	opmode = 0; // enter normal mode of operation
	system("xterm -geometry +1430+18 -e \"tail -f \\\"/tmp/wm$DISPLAY\\\"; "
		"bash\" &");
	system("xterm -geometry +0+18 &");
	system("xterm -geometry +0+338 &");
	system("xterm -geometry +0+658 &");
	// avoid zombie processes by ignoring SIGCHILD
	signal(SIGCHLD, SIG_IGN);
	draw();
}

void Wm::draw() {
	// clear
	xcb_rectangle_t clear_rect[1] = {{ 0, 0, 1920, 20 }};
	xcb_poly_fill_rectangle(conn, rootwin, bg, 1, clear_rect);
	draw_text(mono1, 0, 10, status);
	draw_text(sans1, 1200, 10, lastev);
//	xcb_image_text_8_checked(conn, strlen(status),
//					rootwin, sans1, 0, 10, status);
//	xcb_image_text_8_checked(conn, strlen(lastev),
//					rootwin, sans1, 200, 10, lastev);
	xcb_flush(conn);
}

void Wm::enter_move() {
	opmode = 1;
	switch(child_pid = vfork()) {
	case -1:
		exit(-1);
	case 0: { // child
		char *newargv[] = { (char*)&"y_move", winstr, NULL };
		execve("/usr/bin/y_move", newargv, envp);
		exit(-1); // should never get here
	}
	}
}

void Wm::enter_resize() {
	opmode = 2;
	switch(child_pid = vfork()) {
	case -1:
		exit(-1);
	case 0: { // child
		char *newargv[] = { (char*)&"y_resize", winstr, NULL };
		execve("/usr/bin/y_resize", newargv, envp);
		exit(-1); // should never get here
	}
	}
}

void Wm::print_status(const char *s) {
	xcb_image_text_8_checked(conn, strlen(s), rootwin, mono1, 300, 10, s);
	xcb_flush(conn);
}

void Wm::event_loop() {
	xcb_generic_event_t *ev;
	while(1) {
		int key = 0;
		ev = xcb_wait_for_event(conn);
		if(strlen(lastev) < 100) {
			snprintf(lastev, 1023, "Events: %s %2d",
				lastev + 8, ev->response_type & ~0x80);
		} else {
			snprintf(lastev, 1023, "Events: %s %2d",
				lastev + 11, ev->response_type & ~0x80);
		};
		draw();
		switch(ev->response_type & ~0x80) {
		case XCB_EXPOSE:
			draw();
			break;
		case XCB_KEY_PRESS: {
			xcb_key_press_event_t *kp =
				(xcb_key_press_event_t *)ev;
			key = kp->detail;
			char s[1024];
			snprintf(s, 1023, "Key pressed: %d, %d          ",
				key, kp->state);
			xcb_image_text_8_checked(conn, strlen(s), rootwin,
				sans1, 1000, 500, s);
			xcb_flush(conn);
			break;
		}
		case XCB_KEY_RELEASE: {
			xcb_key_release_event_t *kr =
				(xcb_key_release_event_t *)ev;
			key = kr->detail;
			if(key == 36 && (kr->state & XCB_MOD_MASK_4)) {
				system("xterm&");
			}
			if(key == 22 && (kr->state & XCB_MOD_MASK_CONTROL |
							XCB_MOD_MASK_1)) {
				return; // Ctrl+Alt_Backspace = exit X11
			}
		}
		case XCB_BUTTON_PRESS: {
			xcb_button_press_event_t *bp =
				(xcb_button_press_event_t *)ev;
			char s[1024];
			snprintf(s, 1023, "Button pressed: %d, %d           ",
					bp->detail, bp->state);
			xcb_image_text_8_checked(conn, strlen(s), rootwin,
				sans1, 1000, 500, s);
			xcb_flush(conn);
//			print_status("hello");

			switch(bp->detail) {
			case 2: { // middle mouse button: fullscreen if in move
				switch(opmode) {
				case OP_MOVE:
					kill(child_pid, 9); // quit move/resize
					win = bp->child;
					map<int, Wdata>::iterator it;
					it = wdata.find(win);
					if(it == wdata.end()) {
						break; // not in our database
					}
					Wdata &wd = it->second;
					uint32_t values[4];
					if(wd.flag & 2) { // already full scr
						values[0] = wd.x;
						values[1] = wd.y;
						values[2] = wd.w;
						values[3] = wd.h;
						wd.flag &= ~2;
					} else {
						values[0] = 0;
						values[1] = 0;
						values[2] =
							screen->width_in_pixels;
						values[3] =
						screen->height_in_pixels;
						wd.flag |= 2;
					}
					xcb_configure_window(conn, win,
					XCB_CONFIG_WINDOW_X |
					XCB_CONFIG_WINDOW_Y |
					XCB_CONFIG_WINDOW_WIDTH |
					XCB_CONFIG_WINDOW_HEIGHT, values);
					break;
				}
				break;
			}
			case 3: // if moving, enter the resize window mode
				switch(opmode) {
				case OP_MOVE:
					snprintf(status, 1023, "resizing     ");
					draw();
					// first, terminate the move mode
					kill(child_pid, 9); // stop moving win
					// run the program that binds mouse 2 sz
					enter_resize();
					break;
				case OP_AUX:
					// run menu app
					break;
				}
				break;
			case 4:
				switch(opmode) {
				case OP_MOVE: // kill app
					xcb_kill_client(conn, win);
					xcb_flush(conn);
					break;
				}
				break;
			case 5:
				switch(opmode) {
				case OP_MOVE: { // kill app
					xcb_client_message_event_t oev;
					oev.response_type = XCB_CLIENT_MESSAGE;
					oev.format = 32;
					oev.sequence = 0;
					oev.type = wm_protocols;
					oev.window = win;
					oev.data.data32[0] = wm_delete_window;
					oev.data.data32[1] = XCB_CURRENT_TIME;

					xcb_send_event(conn, false, win,
						XCB_EVENT_MASK_NO_EVENT,
						(char *) &oev);
					xcb_flush(conn);
				}
				}
				break;
			case 8: { // enter the move window mode
				if(opmode) break; // activated from normal mode
				win = bp->child;
				snprintf(winstr, 19, "%d", win);
				xcb_grab_pointer(conn, 0, rootwin,
					XCB_EVENT_MASK_BUTTON_PRESS |
					XCB_EVENT_MASK_BUTTON_RELEASE,
					XCB_GRAB_MODE_ASYNC,
					XCB_GRAB_MODE_ASYNC,
					rootwin, XCB_NONE,
					XCB_CURRENT_TIME);
				// raise this window first
				uint32_t values[3] = {XCB_STACK_MODE_ABOVE, 0};
				xcb_configure_window(conn, win,
						XCB_CONFIG_WINDOW_STACK_MODE,
						values);
				// ewmh way of doing that:
				xcb_ewmh_request_change_active_window(&ewconn,
					mainscreen, win,
					XCB_EWMH_CLIENT_SOURCE_TYPE_OTHER,
					XCB_CURRENT_TIME, XCB_NONE);
				// set input focus to this window
				xcb_set_input_focus(conn,
					XCB_INPUT_FOCUS_POINTER_ROOT, win,
					XCB_CURRENT_TIME);

				xcb_flush(conn);
				// run the program that binds mouse to win pos
				enter_move();
				break;
			}
			case 9: { // enter auxillary mode
				if(opmode) break; // activated from normal mode
				opmode = OP_AUX;
				win = bp->child;
				snprintf(winstr, 19, "%d", win);
				snprintf(status, 1023, "Aux mode: %s          ",
							winstr);

				xcb_grab_pointer(conn, 0, rootwin,
					XCB_EVENT_MASK_BUTTON_PRESS |
					XCB_EVENT_MASK_BUTTON_RELEASE,
					XCB_GRAB_MODE_ASYNC,
					XCB_GRAB_MODE_ASYNC,
					rootwin, XCB_NONE,
					XCB_CURRENT_TIME);

				// raise this window first
				uint32_t values[3] = {XCB_STACK_MODE_ABOVE, 0};
				xcb_configure_window(conn, win,
						XCB_CONFIG_WINDOW_STACK_MODE,
						values);
				// ewmh way of doing that:
				xcb_ewmh_request_change_active_window(&ewconn,
					mainscreen, win,
					XCB_EWMH_CLIENT_SOURCE_TYPE_OTHER,
					XCB_CURRENT_TIME, XCB_NONE);
				// set input focus to this window
				xcb_set_input_focus(conn,
					XCB_INPUT_FOCUS_POINTER_ROOT, win,
					XCB_CURRENT_TIME);

				xcb_flush(conn);
				break;
			}
			}
			break;
		}
		case XCB_BUTTON_RELEASE: {
			xcb_button_release_event_t *br =
				(xcb_button_release_event_t *)ev;
			switch(opmode) {
			case 1: // we are in the move window opreating mode
				if(br->detail != 8) break; // wrong button
				opmode = 0; // normal mode of operation
				xcb_ungrab_pointer(conn, XCB_CURRENT_TIME);
				xcb_flush(conn);
				kill(child_pid, 9); // stop moving window
				break;
			case 2: // we are in resize window mode
				if(br->detail == 8) { // cancel, enter normal op
					opmode = 0; // normal operating mode
					xcb_ungrab_pointer(conn,
							XCB_CURRENT_TIME);
					xcb_flush(conn);
					kill(child_pid, 9);
					break;
				}
				if(br->detail == 3) { // stop resize, enter move
					kill(child_pid, 9); // stop resizing
					// start moving
					enter_move();
					break;
				}
				break;
			case OP_AUX: // we are in auxillary mode
				if(br->detail != 9) break; // wrong button
				opmode = OP_NORMAL;
				xcb_ungrab_pointer(conn, XCB_CURRENT_TIME);
				xcb_flush(conn);
				break;
			}
			break;
		}
		case XCB_CONFIGURE_NOTIFY: {
			xcb_configure_notify_event_t *e =
				(xcb_configure_notify_event_t *)ev;
			map<int, Wdata>::iterator it;
			it = wdata.find(e->window);
			if(it == wdata.end()) {
				//log << "Not in DB" << endl;
				break; // not in our database
			}
			Wdata &wd = it->second;
			if(wd.flag & 1) {
				//log << "Override Redirect" << endl;
				break; // override_redirect flag is on
			}
			if(wd.flag & 2) { // window entered full screen mode
				break;
			}
			wd.x = e->x;
			wd.y = e->y;
			wd.w = e->width;
			wd.h = e->height;
			break;
		}
		case XCB_MAP_NOTIFY: {
			xcb_map_notify_event_t *e =
				(xcb_map_notify_event_t *)ev;
			// check if this window is in our database
			map<int, Wdata>::iterator it;
			it = wdata.find(e->window);
			if(it == wdata.end()) {
				//log << "Not in DB" << endl;
				break; // not in our database
			}
			Wdata &wd = it->second;
			if(wd.flag & 1) {
				//log << "Override Redirect" << endl;
				break; // override_redirect flag is on
			}
			// if intended position is 0, 0, but not fullscreen
			// set position to 60, 30 from top right corner:
			if(wd.x == 0 && wd.y == 0 &&
				wd.w < screen->width_in_pixels &&
				wd.h < screen->height_in_pixels) {
				wd.x = screen->width_in_pixels - wd.w
					- offset_x;
				// simple window stacking scheme:
				wd.y = offset_y;
				offset_x += 5;
				offset_y += 5;
				if(offset_x > 100) {
					offset_x = 60;
				}
				if(offset_y > 80) {
					offset_y = 20;
				}

				uint32_t values[2];
				values[0] = wd.x; values[1] = wd.y;
				xcb_configure_window(conn, e->window,
					XCB_CONFIG_WINDOW_X |
					XCB_CONFIG_WINDOW_Y, values);
			}
			xcb_flush(conn);
			log << "Map notify: " << e->event << " " << e->window <<
				endl;
			break;
		}
		case XCB_CREATE_NOTIFY: {
			// a new window created, we want to track its
			// XCB_ENTER_NOTIFY event so that focus follows pointer
			// but first, let's add it to our tracking list wdata:
			xcb_create_notify_event_t *e =
				(xcb_create_notify_event_t *)ev;

			Wdata &wd = wdata[e->window];
			wd.flag = e->override_redirect & 1;
			wd.window = e->window;
			wd.parent = e->parent;
			wd.x = e->x;
			wd.y = e->y;
			wd.w = e->width;
			wd.h = e->height;

			uint32_t mask = XCB_CW_EVENT_MASK;
			uint32_t values[2];
			values[0] = XCB_EVENT_MASK_ENTER_WINDOW;
			xcb_change_window_attributes_checked(conn, e->window,
						mask, values);
			xcb_flush(conn);
			log << "A window created: " << e->window << " "
				<< e->parent << " <" <<int(e->override_redirect)
				<< endl;
			break;
		}
		case XCB_DESTROY_NOTIFY: {
			xcb_destroy_notify_event_t *e =
				(xcb_destroy_notify_event_t *)ev;
			// when a window is destroyed, remove it from our db:
			map<int, Wdata>::iterator it;
			it = wdata.find(e->window);
			if(it == wdata.end()) {
				wdata.erase(it);
			}
			break;
		}
		case XCB_ENTER_NOTIFY: {
			char wname[1024]; // window name
			wname[0] = 0;
			xcb_enter_notify_event_t *e =
				(xcb_enter_notify_event_t *)ev;
			// don't set focus to root window
			log << "Trying focus to window '" << wname << "'" <<
				e->root << " " <<
				e->event << " " <<
				e->child << " " << endl;

			if(e->event == rootwin) {
				//log << "Ignoring root window" << endl;
				break;
			}
			// don't set focus to focuswin
			if(e->event == focuswin) {
				log << "Already focused, but..." << endl;
				//break;
			}
			// check if this window is in our database
			map<int, Wdata>::iterator it;
			it = wdata.find(e->event);
			if(it == wdata.end()) {
				log << "Not in DB" << endl;
				break; // not in our database
			}
			Wdata &wd = it->second;
			if(wd.flag & 1) {
				//log << "Override Redirect" << endl;
				break; // override_redirect flag is on
			}
			// update window title display:
			size_t len = get_window_name(e->event, wname, 1024);
			snprintf(status, 1023, "%s",
				wname, e->root, e->event, e->child);
			// set input focus to this window
			xcb_set_input_focus(conn,
					XCB_INPUT_FOCUS_POINTER_ROOT, e->event,
					XCB_CURRENT_TIME);
			draw(); // draw() flushes for us, no need for xcb_flush

			focuswin = e->event;

			log << "Setting focus to window '" << wname << "'" <<
				e->root << " " <<
				e->event << " " <<
				e->child << " " << wd.flag << " " <<
				len << endl;

			break;
		}
		}
	}
}

size_t Wm::get_window_name(xcb_window_t win, char *buf, size_t len) {
	xcb_get_property_cookie_t cookie;
	xcb_get_property_reply_t *reply;
//	xcb_atom_t property = XCB_ATOM_WM_NAME;
	xcb_atom_t property = ewconn._NET_WM_NAME;
//	xcb_atom_t type = XCB_ATOM_STRING;
	xcb_atom_t type = XCB_ATOM_ANY;
	size_t length;
	cookie = xcb_get_property(conn, 0, win, property, type, 0, 200);
	if((reply = xcb_get_property_reply(
			conn, cookie, NULL))) {
		length = xcb_get_property_value_length(reply);
		if(length != 0) {
			length = (length >= len)? len: length;
			strncpy(buf,
				(char *)xcb_get_property_value(reply), length);
			buf[length] = '\0';
			free(reply);
			return length + 1;
		}
		free(reply);
	}
	// length is zero, try another way to get window name
	property = XCB_ATOM_WM_NAME;
	cookie = xcb_get_property(conn, 0, win, property, type, 0, 200);
	if((reply = xcb_get_property_reply(
			conn, cookie, NULL))) {
		length = xcb_get_property_value_length(reply);
		if(length != 0) {
			length = (length >= len)? len: length;
			strncpy(buf,
				(char *)xcb_get_property_value(reply), length);
			buf[length] = '\0';
			free(reply);
			return length + 1;
		}
		free(reply);
	}
}

int main(int argc, char **argv, char **envp) {
	Wm wm;
	wm.envp = envp; // pass the environment variables
	wm.init();
	wm.event_loop();

	return 0;
}


