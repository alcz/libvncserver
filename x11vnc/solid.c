/* -- solid.c -- */

#include "x11vnc.h"
#include "win_utils.h"
#include "xwrappers.h"
#include "connections.h"
#include "cleanup.h"

char *guess_desktop(void);
void solid_bg(int restore);


static void usr_bin_path(int restore);
static int dt_cmd(char *cmd);
static char *cmd_output(char *cmd);
XImage *solid_root(char *color);
static void solid_cde(char *color);
static void solid_gnome(char *color);
static void solid_kde(char *color);


static void usr_bin_path(int restore) {
	static char *oldpath = NULL;
	char *newpath;
	char addpath[] = "/usr/bin:/bin:";

	if (restore) {
		if (oldpath) {
			set_env("PATH", oldpath);
			free(oldpath);
			oldpath = NULL;
		}
		return;
	}

	if (getenv("PATH")) {
		oldpath = strdup(getenv("PATH"));
	} else {
		oldpath = strdup("/usr/bin");
	}
	newpath = (char *) malloc(strlen(oldpath) + strlen(addpath) + 1);
	newpath[0] = '\0';
	strcat(newpath, addpath);
	strcat(newpath, oldpath);
	set_env("PATH", newpath);
	free(newpath);
}

static int dt_cmd(char *cmd) {
	int rc;

	RAWFB_RET(0)

	if (!cmd || *cmd == '\0') {
		return 0;
	}

	/* dt */
	if (no_external_cmds || !cmd_ok("dt")) {
		rfbLog("cannot run external commands in -nocmds mode:\n");
		rfbLog("   \"%s\"\n", cmd);
		rfbLog("   dt_cmd: returning 1\n");
		return 1;
	}

	if (getenv("DISPLAY") == NULL) {
		set_env("DISPLAY", DisplayString(dpy));
	}

	rfbLog("running command:\n");
	if (!quiet) {
		fprintf(stderr, "\n  %s\n\n", cmd);
	}
	usr_bin_path(0);
	close_exec_fds();
	rc = system(cmd);
	usr_bin_path(1);

	if (rc >= 256) {
		rc = rc/256;
	}
	return rc;
}

static char *cmd_output(char *cmd) {
	FILE *p;
	static char output[50000];
	char line[1024];
	int rc;

	if (!cmd || *cmd == '\0') {
		return "";
	}

	if (no_external_cmds) {
		rfbLog("cannot run external commands in -nocmds mode:\n");
		rfbLog("   \"%s\"\n", cmd);
		rfbLog("   cmd_output: null string.\n");
		return "";
	}

	rfbLog("running pipe:\n");
	if (!quiet) {
		fprintf(stderr, "\n  %s\n\n", cmd);
	}
	usr_bin_path(0);
	close_exec_fds();
	p = popen(cmd, "r");
	usr_bin_path(1);

	output[0] = '\0';

	while (fgets(line, 1024, p) != NULL) {
		if (strlen(output) + strlen(line) + 1 < 50000) {
			strcat(output, line);
		}
	}
	rc = pclose(p);
	return(output);
}

static char *last_color = NULL;

unsigned long get_pixel(char *color) {
#if NO_X11
	return 0;
#else
	XColor cdef;
	Colormap cmap;
	unsigned long pixel = BlackPixel(dpy, scr);
	if (depth > 8 || strcmp(color, solid_default)) {
		cmap = DefaultColormap (dpy, scr);
		if (XParseColor(dpy, cmap, color, &cdef) &&
		    XAllocColor(dpy, cmap, &cdef)) {
			pixel = cdef.pixel;
		} else {
			rfbLog("error parsing/allocing color: %s\n", color);
		}
	}
	return pixel;
#endif
}

XImage *solid_root(char *color) {
#if NO_X11
	RAWFB_RET_VOID
	if (!color) {}
	return NULL;
#else
	Window expose;
	static XImage *image = NULL;
	Pixmap pixmap;
	XGCValues gcv;
	GC gc;
	XSetWindowAttributes swa;
	Visual visual;
	static unsigned long mask, pixel = 0;

	RAWFB_RET(NULL)

	if (subwin || window != rootwin) {
		rfbLog("cannot set subwin to solid color, must be rootwin\n");
		return NULL;
	}

	/* create the "clear" window just for generating exposures */
	swa.override_redirect = True;
	swa.backing_store = NotUseful;
	swa.save_under = False;
	swa.background_pixmap = None;
	visual.visualid = CopyFromParent;
	mask = (CWOverrideRedirect|CWBackingStore|CWSaveUnder|CWBackPixmap);
	expose = XCreateWindow(dpy, window, 0, 0, wdpy_x, wdpy_y, 0, depth,
	    InputOutput, &visual, mask, &swa);

	if (! color) {

		if (! image) {
			/* whoops */
			XDestroyWindow(dpy, expose);
			rfbLog("no root snapshot available.\n");
			return NULL;
		}

		/* restore the root window from the XImage snapshot */
		pixmap = XCreatePixmap(dpy, window, wdpy_x, wdpy_y, depth);
		
		/* draw the image to a pixmap: */
		gcv.function = GXcopy;
		gcv.plane_mask = AllPlanes;
		gc = XCreateGC(dpy, window, GCFunction|GCPlaneMask, &gcv);

		XPutImage(dpy, pixmap, gc, image, 0, 0, 0, 0, wdpy_x, wdpy_y);

		gcv.foreground = gcv.background = BlackPixel(dpy, scr);
		gc = XCreateGC(dpy, window, GCForeground|GCBackground, &gcv);

		rfbLog("restoring root snapshot...\n");
		/* set the pixmap as the bg: */
		XSetWindowBackgroundPixmap(dpy, window, pixmap);
		XFreePixmap(dpy, pixmap);
		XClearWindow(dpy, window);
		XFlush_wr(dpy);
		
		/* generate exposures */
		XMapWindow(dpy, expose);
		XSync(dpy, False);
		XDestroyWindow(dpy, expose);
		return NULL;
	}

	if (! image) {
		/* need to retrieve a snapshot of the root background: */
		Window iwin;
		XSetWindowAttributes iswa;

		/* create image window: */
		iswa.override_redirect = True;
		iswa.backing_store = NotUseful;
		iswa.save_under = False;
		iswa.background_pixmap = ParentRelative;

		iwin = XCreateWindow(dpy, window, 0, 0, wdpy_x, wdpy_y, 0,
		    depth, InputOutput, &visual, mask, &iswa);

		rfbLog("snapshotting background...\n");

		XMapWindow(dpy, iwin);
		XSync(dpy, False);
		image = XGetImage(dpy, iwin, 0, 0, wdpy_x, wdpy_y, AllPlanes,
		    ZPixmap);
		XSync(dpy, False);
		XDestroyWindow(dpy, iwin);
		rfbLog("done.\n");
	}
	if (color == (char *) 0x1) {
		/* caller will XDestroyImage it: */
		XImage *xi = image;
		image = NULL;
		return xi;
	}

	/* use black for low colors or failure */
	pixel = get_pixel(color);

	rfbLog("setting solid background...\n");
	XSetWindowBackground(dpy, window, pixel);
	XMapWindow(dpy, expose);
	XSync(dpy, False);
	XDestroyWindow(dpy, expose);
#endif	/* NO_X11 */
	return NULL;
}

static void solid_cde(char *color) {
#if NO_X11
	RAWFB_RET_VOID
	if (!color) {}
	return;
#else
	int wsmax = 16;
	static XImage *image[16];
	static Window ws_wins[16];
	static int nws = -1;

	Window expose;
	Pixmap pixmap;
	XGCValues gcv;
	GC gc;
	XSetWindowAttributes swa;
	Visual visual;
	unsigned long mask, pixel;
	XColor cdef;
	Colormap cmap;
	int n;

	RAWFB_RET_VOID

	if (subwin || window != rootwin) {
		rfbLog("cannot set subwin to solid color, must be rootwin\n");
		return;
	}

	/* create the "clear" window just for generating exposures */
	swa.override_redirect = True;
	swa.backing_store = NotUseful;
	swa.save_under = False;
	swa.background_pixmap = None;
	visual.visualid = CopyFromParent;
	mask = (CWOverrideRedirect|CWBackingStore|CWSaveUnder|CWBackPixmap);
	expose = XCreateWindow(dpy, window, 0, 0, wdpy_x, wdpy_y, 0, depth,
	    InputOutput, &visual, mask, &swa);

	if (! color) {
		/* restore the backdrop windows from the XImage snapshots */

		for (n=0; n < nws; n++) {
			Window twin;

			if (! image[n]) {
				continue;
			}

			twin = ws_wins[n];
			if (! twin) {
				twin = rootwin;
			}
			if (! valid_window(twin, NULL, 0)) {
				continue;
			}

			pixmap = XCreatePixmap(dpy, twin, wdpy_x, wdpy_y,
			    depth);
			
			/* draw the image to a pixmap: */
			gcv.function = GXcopy;
			gcv.plane_mask = AllPlanes;
			gc = XCreateGC(dpy, twin, GCFunction|GCPlaneMask, &gcv);

			XPutImage(dpy, pixmap, gc, image[n], 0, 0, 0, 0,
			    wdpy_x, wdpy_y);

			gcv.foreground = gcv.background = BlackPixel(dpy, scr);
			gc = XCreateGC(dpy, twin, GCForeground|GCBackground,
			    &gcv);

			rfbLog("restoring CDE ws%d snapshot to 0x%lx\n",
			    n, twin);
			/* set the pixmap as the bg: */
			XSetWindowBackgroundPixmap(dpy, twin, pixmap);
			XFreePixmap(dpy, pixmap);
			XClearWindow(dpy, twin);
			XFlush_wr(dpy);
		}
		
		/* generate exposures */
		XMapWindow(dpy, expose);
		XSync(dpy, False);
		XDestroyWindow(dpy, expose);
		return;
	}

	if (nws < 0) {
		/* need to retrieve snapshots of the ws backgrounds: */
		Window iwin, wm_win;
		XSetWindowAttributes iswa;
		Atom dt_list, wm_info, type;
		int format;
		unsigned long length, after;
		unsigned char *data;
		unsigned long *dp;	/* crash on 64bit with int */

		nws = 0;

		/* extract the hidden wm properties about backdrops: */

		wm_info = XInternAtom(dpy, "_MOTIF_WM_INFO", True);
		if (wm_info == None) {
			return;
		}

		XGetWindowProperty(dpy, rootwin, wm_info, 0L, 10L, False,
		    AnyPropertyType, &type, &format, &length, &after, &data);

		/*
		 * xprop -notype -root _MOTIF_WM_INFO
		 * _MOTIF_WM_INFO = 0x2, 0x580028
		 */

		if (length < 2 || format != 32 || after != 0) {
			return;
		}

		dp = (unsigned long *) data;
		wm_win = (Window) *(dp+1);	/* 2nd item. */


		dt_list = XInternAtom(dpy, "_DT_WORKSPACE_LIST", True);
		if (dt_list == None) {
			return;
		}

		XGetWindowProperty(dpy, wm_win, dt_list, 0L, 10L, False,
		   AnyPropertyType, &type, &format, &length, &after, &data);

		nws = length;

		if (nws > wsmax) {
			nws = wsmax;
		}
		if (nws < 0) {
			nws = 0;
		}

		rfbLog("special CDE win: 0x%lx, %d workspaces\n", wm_win, nws);
		if (nws == 0) {
			return;
		}

		for (n=0; n<nws; n++) {
			Atom ws_atom;
			char tmp[32];
			Window twin;
			XWindowAttributes attr;
			int i, cnt;

			image[n] = NULL;
			ws_wins[n] = 0x0;

			sprintf(tmp, "_DT_WORKSPACE_INFO_ws%d", n);
			ws_atom = XInternAtom(dpy, tmp, False);
			if (ws_atom == None) {
				continue;
			}
			XGetWindowProperty(dpy, wm_win, ws_atom, 0L, 100L,
			   False, AnyPropertyType, &type, &format, &length,
			   &after, &data);

			if (format != 8 || after != 0) {
				continue;
			}
			/*
			 * xprop -notype -id wm_win
			 * _DT_WORKSPACE_INFO_ws0 = "One", "3", "0x2f2f4a",
			 * "0x63639c", "0x103", "1", "0x58044e"
			 */

			cnt = 0;
			twin = 0x0;
			for (i=0; i< (int) length; i++) {
				if (*(data+i) != '\0') {
					continue;
				}
				cnt++;	/* count nulls to indicate field */
				if (cnt == 6) {
					/* one past the null: */
					char *q = (char *) (data+i+1);
					unsigned long in;
					if (sscanf(q, "0x%lx", &in) == 1) {
						twin = (Window) in;
						break;
					}
				}
			}
			ws_wins[n] = twin;

			if (! twin) {
				twin = rootwin;
			}

			XGetWindowAttributes(dpy, twin, &attr);
			if (twin != rootwin) {
				if (attr.map_state != IsViewable) {
					XMapWindow(dpy, twin);
				}
				XRaiseWindow(dpy, twin);
			}
			XSync(dpy, False);
		
			/* create image window: */
			iswa.override_redirect = True;
			iswa.backing_store = NotUseful;
			iswa.save_under = False;
			iswa.background_pixmap = ParentRelative;
			visual.visualid = CopyFromParent;

			iwin = XCreateWindow(dpy, twin, 0, 0, wdpy_x, wdpy_y,
			    0, depth, InputOutput, &visual, mask, &iswa);

			rfbLog("snapshotting CDE backdrop ws%d 0x%lx -> "
			    "0x%lx ...\n", n, twin, iwin);
			XMapWindow(dpy, iwin);
			XSync(dpy, False);

			image[n] = XGetImage(dpy, iwin, 0, 0, wdpy_x, wdpy_y,
			    AllPlanes, ZPixmap);
			XSync(dpy, False);
			XDestroyWindow(dpy, iwin);
			if (twin != rootwin) {
				XLowerWindow(dpy, twin);
				if (attr.map_state != IsViewable) {
					XUnmapWindow(dpy, twin);
				}
			}
		}
	}
	if (nws == 0) {
		return;
	}

	/* use black for low colors or failure */
	pixel = BlackPixel(dpy, scr);
	if (depth > 8 || strcmp(color, solid_default)) {
		cmap = DefaultColormap (dpy, scr);
		if (XParseColor(dpy, cmap, color, &cdef) &&
		    XAllocColor(dpy, cmap, &cdef)) {
			pixel = cdef.pixel;
		} else {
			rfbLog("error parsing/allocing color: %s\n", color);
		}
	}

	rfbLog("setting solid backgrounds...\n");

	for (n=0; n < nws; n++)  {
		Window twin = ws_wins[n];
		if (image[n] == NULL) {
			continue;
		}
		if (! twin)  {
			twin = rootwin;
		}
		XSetWindowBackground(dpy, twin, pixel);
	}
	XMapWindow(dpy, expose);
	XSync(dpy, False);
	XDestroyWindow(dpy, expose);
#endif	/* NO_X11 */
}

static void solid_gnome(char *color) {
#if NO_X11
	RAWFB_RET_VOID
	if (!color) {}
	return;
#else
	char get_color[] = "gconftool-2 --get "
	    "/desktop/gnome/background/primary_color";
	char set_color[] = "gconftool-2 --set "
	    "/desktop/gnome/background/primary_color --type string '%s'";
	char get_option[] = "gconftool-2 --get "
	    "/desktop/gnome/background/picture_options";
	char set_option[] = "gconftool-2 --set "
	    "/desktop/gnome/background/picture_options --type string '%s'";
	static char *orig_color = NULL;
	static char *orig_option = NULL;
	char *cmd;

	RAWFB_RET_VOID
	
	if (! color) {
		if (! orig_color) {
			orig_color = strdup("#FFFFFF");
		}
		if (! orig_option) {
			orig_option = strdup("stretched");
		}
		if (strstr(orig_color, "'") != NULL)  {
			rfbLog("invalid color: %s\n", orig_color);
			return;
		}
		if (strstr(orig_option, "'") != NULL)  {
			rfbLog("invalid option: %s\n", orig_option);
			return;
		}
		cmd = (char *) malloc(strlen(set_option) - 2 +
		    strlen(orig_option) + 1);
		sprintf(cmd, set_option, orig_option);
		dt_cmd(cmd);
		free(cmd);
		cmd = (char *) malloc(strlen(set_color) - 2 +
		    strlen(orig_color) + 1);
		sprintf(cmd, set_color, orig_color);
		dt_cmd(cmd);
		free(cmd);
		return;
	}

	if (! orig_color) {
		char *q;
		if (cmd_ok("dt")) {
			orig_color = strdup(cmd_output(get_color));
		} else {
			orig_color = "";
		}
		if (*orig_color == '\0') {
			orig_color = strdup("#FFFFFF");
		}
		if ((q = strchr(orig_color, '\n')) != NULL) {
			*q = '\0';
		}
	}
	if (! orig_option) {
		char *q;
		if (cmd_ok("dt")) {
			orig_option = strdup(cmd_output(get_option));
		} else {
			orig_color = "";
		}
		if (*orig_option == '\0') {
			orig_option = strdup("stretched");
		}
		if ((q = strchr(orig_option, '\n')) != NULL) {
			*q = '\0';
		}
	}
	if (strstr(color, "'") != NULL)  {
		rfbLog("invalid color: %s\n", color);
		return;
	}
	cmd = (char *) malloc(strlen(set_color) + strlen(color) + 1);
	sprintf(cmd, set_color, color);
	dt_cmd(cmd);
	free(cmd);

	cmd = (char *) malloc(strlen(set_option) + strlen("none") + 1);
	sprintf(cmd, set_option, "none");
	dt_cmd(cmd);
	free(cmd);
#endif	/* NO_X11 */
}

static char *dcop_session(void) {
	char *empty = strdup("");
#if NO_X11
	RAWFB_RET(empty);
	return empty;
#else
	char list_sessions[] = "dcop --user '%s' --list-sessions";
	int len;
	char *cmd, *host, *user = NULL;
	char *out, *p, *ds, *dsn = NULL, *sess = NULL, *sess2 = NULL;

	RAWFB_RET(empty);

	if (getenv("SESSION_MANAGER")) {
		return empty;
	}

	user = get_user_name();
	if (strstr(user, "'") != NULL)  {
		rfbLog("invalid user: %s\n", user);
		free(user);
		return empty;
	}

	len = strlen(list_sessions) + strlen(user) + 1;
	cmd = (char *) malloc(len);
	sprintf(cmd, list_sessions, user);

	out = strdup(cmd_output(cmd));
	free(cmd);
	free(user);

	ds = DisplayString(dpy);
	if (!ds || !strcmp(ds, "")) {
		ds = getenv("DISPLAY");
	}
	if (!ds) {
		ds = ":0";
	}
	ds = strdup(ds);
	dsn = strchr(ds, ':');
	if (dsn) {
		*dsn = '_';
	} else {
		free(ds);
		ds = strdup("_0");
		dsn = ds;
	}

	host = this_host();

	p = strtok(out, "\n");
	while (p) {
		char *q = strstr(p, ".DCOP");
		if (q == NULL) {
			;
		} else if (host) {
			if (strstr(q, host)) {
				if(strstr(p, dsn)) {
					sess = strdup(q);
					break;
				} else {
					if (sess2) {
						free(sess2);
					} 
					sess2 = strdup(q);
				}
			}
		} else {
			if(strstr(p, dsn)) {
				sess = strdup(q);
				break;
			}
		}
		p = strtok(NULL, "\n");
	}
	free(ds);
	free(out);
	if (!sess && sess2) {
		sess = sess2;
	}
	if (!sess || strchr(sess, '\'')) {
		if (sess) free(sess);
		sess = strdup("--all-sessions");
	} else {
		len = strlen("--session ") + 2 + strlen(sess) + 1;
		cmd = (char *) malloc(len);
		sprintf(cmd, "--session '%s'", sess);
		free(sess);
		sess = cmd;
	}
	return sess;
#endif
}

static void solid_kde(char *color) {
#if NO_X11
	RAWFB_RET_VOID
	if (!color) {}
	return;
#else
	char set_color[] =
	    "dcop --user '%s' %s kdesktop KBackgroundIface setColor '%s' 1";
	char bg_off[] =
	    "dcop --user '%s' %s kdesktop KBackgroundIface setBackgroundEnabled 0";
	char bg_on[] =
	    "dcop --user '%s' %s kdesktop KBackgroundIface setBackgroundEnabled 1";
	char *cmd, *user = NULL, *sess;
	int len;

	RAWFB_RET_VOID

	user = get_user_name();
	if (strstr(user, "'") != NULL)  {
		rfbLog("invalid user: %s\n", user);
		free(user);
		return;
	}

	set_env("DISPLAY", DisplayString(dpy));

	if (! color) {
		sess = dcop_session();
		len = strlen(bg_on) + strlen(user) + strlen(sess) + 1;
		cmd = (char *) malloc(len);
		sprintf(cmd, bg_on, user, sess);

		dt_cmd(cmd);

		free(cmd);
		free(user);
		free(sess);

		return;
	}

	if (strstr(color, "'") != NULL)  {
		rfbLog("invalid color: %s\n", color);
		return;
	}

	sess = dcop_session();

	len = strlen(set_color) + strlen(user) + strlen(sess) + strlen(color) + 1;
	cmd = (char *) malloc(len);
	sprintf(cmd, set_color, user, sess, color);
	dt_cmd(cmd);
	free(cmd);

	len = strlen(bg_off) + strlen(user) + strlen(sess) + 1;
	cmd = (char *) malloc(len);
	sprintf(cmd, bg_off, user, sess);
	dt_cmd(cmd);
	free(cmd);
	free(user);
#endif	/* NO_X11 */
}

void kde_no_animate(int restore) {
#if NO_X11
	if (!restore) {}
	RAWFB_RET_VOID
	return;
#else
	char query_setting[] =
	    "kreadconfig  --file kwinrc --group Windows --key AnimateMinimize";
	char kwinrc_off[] =
	    "kwriteconfig --file kwinrc --group Windows --key AnimateMinimize --type bool false";
	char kwinrc_on[] =
	    "kwriteconfig --file kwinrc --group Windows --key AnimateMinimize --type bool true";
	char kwin_reconfigure[] =
	    "dcop --user '%s' %s kwin KWinInterface reconfigure";
	char *cmd, *cmd2, *out, *user = NULL, *sess;
	int len;
	static int anim_state = 1;

	RAWFB_RET_VOID

	if (ncache_keep_anims) {
		return;
	}

	if (restore) {
		if (anim_state == 1) {
			return;
		}

		user = get_user_name();
		if (strstr(user, "'") != NULL)  {
			rfbLog("invalid user: %s\n", user);
			free(user);
			return;
		}

		sess = dcop_session();

		len = strlen(kwin_reconfigure) + strlen(user) + strlen(sess) + 1;
		cmd = (char *) malloc(len);
		sprintf(cmd, kwin_reconfigure, user, sess);
		rfbLog("\n");
		rfbLog("Restoring KDE kwinrc settings.\n");
		rfbLog("\n");
		dt_cmd(cmd);
		free(cmd);
		free(user);
		free(sess);
		anim_state = 1;
		return;
	} else {
		if (anim_state == 0) {
			return;
		}
		anim_state = 0;
	}

	user = get_user_name();
	if (strstr(user, "'") != NULL)  {
		rfbLog("invalid user: %s\n", user);
		free(user);
		return;
	}
	out = cmd_output(query_setting);


	if (!out || strstr(out, "false")) {
		rfbLog("\n");
		rfbLog("********************************************************\n");
		rfbLog("KDE kwinrc AnimateMinimize is false. Good.\n");
		rfbLog("********************************************************\n");
		rfbLog("\n");
		free(user);
		return;
	}

	rfbLog("\n");
	rfbLog("********************************************************\n");
	rfbLog("To improve the -ncache client-side caching performance\n");
	rfbLog("temporarily setting KDE kwinrc AnimateMinimize to false.\n");
	rfbLog("It will be reset for the next session or when VNC client\n");
	rfbLog("disconnects.  Or you can use the Control Center GUI to\n");
	rfbLog("change it now (toggle its setting a few times):\n");
	rfbLog("   Desktop -> Window Behavior -> Moving\n");
	rfbLog("********************************************************\n");
	rfbLog("\n");

	set_env("DISPLAY", DisplayString(dpy));

	sess = dcop_session();
	len = strlen(kwin_reconfigure) + strlen(user) + strlen(sess) + 1;
	cmd = (char *) malloc(len);
	sprintf(cmd, kwin_reconfigure, user, sess);

	len = 1 + strlen(kwinrc_off) + 2 + strlen(cmd) + 2 + strlen("sleep 5") + 2 + strlen(kwinrc_on) + 3 + 1;
	cmd2 = (char *) malloc(len);

	sprintf(cmd2, "(%s; %s; sleep 5; %s) &", kwinrc_off, cmd, kwinrc_on);

	dt_cmd(cmd2);
	free(cmd);
	free(cmd2);
	free(user);
	free(sess);
#endif	/* NO_X11 */
}

void gnome_no_animate(void) {
	;
}

char *guess_desktop(void) {
#if NO_X11
	RAWFB_RET("root")
	return "root";
#else
	Atom prop;

	RAWFB_RET("root")

	if (wmdt_str && *wmdt_str != '\0') {
		char *s = wmdt_str;
		lowercase(s);
		if (strstr(s, "xfce")) {
			return "xfce";
		}
		if (strstr(s, "gnome") || strstr(s, "metacity")) {
			return "gnome";
		}
		if (strstr(s, "kde") || strstr(s, "kwin")) {
			return "kde";
		}
		if (strstr(s, "cde")) {
			return "cde";
		}
		return "root";
	}

	if (! dpy) {
		return "";
	}

	prop = XInternAtom(dpy, "XFCE_DESKTOP_WINDOW", True);
	if (prop != None) return "xfce";

	/* special case windowmaker */
	prop = XInternAtom(dpy, "_WINDOWMAKER_WM_PROTOCOLS", True);
	if (prop != None)  return "root";

	prop = XInternAtom(dpy, "_WINDOWMAKER_COMMAND", True);
	if (prop != None) return "root";

	prop = XInternAtom(dpy, "NAUTILUS_DESKTOP_WINDOW_ID", True);
	if (prop != None) return "gnome";

	prop = XInternAtom(dpy, "KWIN_RUNNING", True);
	if (prop != None) {
		prop = XInternAtom(dpy, "_KDE_RUNNING", True);
		if (prop != None) {
			prop = XInternAtom(dpy, "KDE_DESKTOP_WINDOW", True);
			if (prop != None) return "kde";
		}
	}

	prop = XInternAtom(dpy, "_MOTIF_WM_INFO", True);
	if (prop != None) {
		prop = XInternAtom(dpy, "_DT_WORKSPACE_LIST", True);
		if (prop != None) return "cde";
	}
	return "root";
#endif	/* NO_X11 */
}

XImage *solid_image(char *color) {
#if NO_X11
	RAWFB_RET(NULL)
	return NULL;
#else
	XImage *image = NULL;
	unsigned long pixel = 0;
	int x, y;

	RAWFB_RET(NULL)

	if (!color) {
		color = last_color;
	}

	if (!color) {
		return NULL;
	}

	image = XGetImage(dpy, rootwin, 0, 0, wdpy_x, wdpy_y, AllPlanes,
	    ZPixmap);

	if (!image) {
		return NULL;
	}
	pixel = get_pixel(color);

	for (y=0; y<wdpy_y; y++) {
		for (x=0; x<wdpy_x; x++) {
			XPutPixel(image, x, y, pixel);
		}
	}
	return image;
#endif	/* NO_X11 */
}

void solid_bg(int restore) {
	static int desktop = -1;
	static int solid_on = 0;
	static char *prev_str;
	char *dtname, *color;

	RAWFB_RET_VOID

	if (started_as_root == 1 && users_list) {
		/* we are still root, don't try. */
		return;
	}

	if (restore) {
		if (! solid_on) {
			return;
		}
		if (desktop == 0) {
			solid_root(NULL);
		} else if (desktop == 1) {
			solid_gnome(NULL);
		} else if (desktop == 2) {
			solid_kde(NULL);
		} else if (desktop == 3) {
			solid_cde(NULL);
		}
		solid_on = 0;
		return;
	}
	if (! solid_str) {
		return;
	}
	if (solid_on && !strcmp(prev_str, solid_str)) {
		return;
	}
	if (strstr(solid_str, "guess:") == solid_str
	    || !strchr(solid_str, ':')) {
		dtname = guess_desktop();
		rfbLog("guessed desktop: %s\n", dtname);
	} else {
		if (strstr(solid_str, "gnome:") == solid_str) {
			dtname = "gnome";
		} else if (strstr(solid_str, "kde:") == solid_str) {
			dtname = "kde";
		} else if (strstr(solid_str, "cde:") == solid_str) {
			dtname = "cde";
		} else {
			dtname = "root";
		}
	}

	color = strchr(solid_str, ':');
	if (! color) {
		color = solid_str;
	} else {
		color++;
		if (*color == '\0') {
			color = solid_default;
		}
	}
	if (last_color) {
		free(last_color);
	}
	last_color = strdup(color);

	if (!strcmp(dtname, "gnome")) {
		desktop = 1;
		solid_gnome(color);
	} else if (!strcmp(dtname, "kde")) {
		desktop = 2;
		solid_kde(color);
	} else if (!strcmp(dtname, "cde")) {
		desktop = 3;
		solid_cde(color);
	} else {
		desktop = 0;
		solid_root(color);
	}
	if (prev_str) {
		free(prev_str);
	}
	prev_str = strdup(solid_str);
	solid_on = 1;
}

