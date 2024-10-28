#include "clock.h"
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <argp.h>
#include <mpv/client.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef VERSION
#define VERSION "development"
#endif

static void die(const char *msg) {
  fprintf(stderr, "%s\n", msg);
  exit(1);
}

static int time_now() { return (int)time(NULL); }

// X11

static int on_x11_error(Display *d, XErrorEvent *e) {
  fprintf(stderr, "xlib: %d\n", e->error_code);
  return 0;
}

Display *open_display() {
  XSetErrorHandler(on_x11_error);
  return XOpenDisplay(NULL);
}

Window create_root_window(Display *display) {
  Window root = XDefaultRootWindow(display);

  XWindowAttributes root_window_attribute;
  XGetWindowAttributes(display, root, &root_window_attribute);

  Window root_window =
      XCreateSimpleWindow(display, root, 0, 0, root_window_attribute.width,
                          root_window_attribute.height, 0, 0, 0);

  XSelectInput(display, root_window, StructureNotifyMask | KeyPressMask);

  return root_window;
}

Window create_sub_window(Display *display, Window root_window) {
  Window sub_window =
      XCreateSimpleWindow(display, root_window, 0, 0, 0, 0, 0, 0, 0);
  return sub_window;
}

Atom create_delete_window_atom(Display *display, Window window) {
  Atom wm_delete_window = XInternAtom(display, "WM_DELETE_WINDOW", False);
  XSetWMProtocols(display, window, &wm_delete_window, 1);
  return wm_delete_window;
}

// MPV

const char *MPV_PROPERTY_DEMUXER_CACHE_TIME = "demuxer-cache-time";
const char *MPV_PROPERTY_TIME_REMAINING = "time-remaining";

const double max_delay_s = 0.5;
const double min_delay_s = 0.1;
const int timeout_s = 5;

struct MpvState {
  char *file;
  double speed;
  int speed_updated_at;
  int pinged_at;
};

mpv_handle *setup_mpv(Window window, char *file, char *hwdec) {
  mpv_handle *mpv = mpv_create();
  if (mpv == NULL)
    die("mpv context failed");

  mpv_set_option(mpv, "wid", MPV_FORMAT_INT64, &window);
  mpv_set_option_string(mpv, "profile", "low-latency");
  mpv_set_option_string(mpv, "cache", "now");
  if (hwdec)
    mpv_set_option_string(mpv, "hwdec", hwdec);

  mpv_observe_property(mpv, 0, MPV_PROPERTY_TIME_REMAINING, MPV_FORMAT_DOUBLE);
  mpv_observe_property(mpv, 0, MPV_PROPERTY_DEMUXER_CACHE_TIME,
                       MPV_FORMAT_DOUBLE);

  if (mpv_initialize(mpv) < 0)
    die("mpv init failed");

  mpv_request_log_messages(mpv, "info");

  const char *cmd[] = {"loadfile", file, NULL};
  if (mpv_command(mpv, cmd) < 0)
    die("mpv failed to play file");

  return mpv;
}

// CLI

#define MAX_FILES 32

struct Arguments {
  char *hwdec;
  char *files[MAX_FILES];
};

static struct argp_option options[] = {
    {"version", 'v', 0, 0, "Show version"},
    {"hwdec", 1, "HWDEC", 0, "Set hwdec mpv option"},
    {0}};

static int parser(int key, char *arg, struct argp_state *state) {
  struct Arguments *arguments = state->input;

  switch (key) {
  case 'v':
    printf("%s\n", VERSION);
    exit(0);
  case 1:
    arguments->hwdec = arg;
    break;
  case ARGP_KEY_ARG:
    for (int i = 0; i < MAX_FILES; i++) {
      if (arguments->files[i] == NULL) {
        arguments->files[i] = arg;
        break;
      }
    }
    break;
  }
  return 0;
}

int main(int argc, char *argv[]) {
  struct Arguments arguments;

  struct argp argp = {options, parser, 0, 0};
  argp_parse(&argp, argc, argv, 0, 0, &arguments);

  if (arguments.files[0] == NULL)
    die("no media files specified");

  Display *display = open_display();
  if (display == NULL)
    die("failed to open display");

  Window root_window = create_root_window(display);
  if (root_window == 0)
    die("failed to create window");

  Atom wm_delete_window = create_delete_window_atom(display, root_window);

  XMapWindow(display, root_window);

  mpv_handle *mpv = setup_mpv(root_window, arguments.files[0], arguments.hwdec);
  struct MpvState mpv_state = {
      .file = arguments.files[0],
      .speed = 1.0,
      .speed_updated_at = time_now(),
      .pinged_at = time_now(),
  };

  clock_set_fps(60);

  int quit = False;
  while (!quit) {
    clock_start();

    // X11 events
    while (XPending(display)) {
      XEvent event;
      XNextEvent(display, &event);
      switch (event.type) {
      case ClientMessage:
        if (event.xclient.data.l[0] == wm_delete_window) {
          fprintf(stderr, "quitting: \n");
          quit = True;
        }
        break;
      case KeyPress: {
        fprintf(stderr, "KeyPress: %d\n", event.xkey.keycode);
        if (event.xkey.keycode == XKeysymToKeycode(display, XK_q)) {
          quit = True;
        }
        break;
      }
      }
    }

    double new_speed = 0;
    Bool ping = False;
    Bool reload_file = False;

    // MPV speed timeout
    if (time_now() > mpv_state.speed_updated_at + 5)
      new_speed = 1.0;

    // MPV watchdog
    if (time_now() > (mpv_state.pinged_at + timeout_s))
      reload_file = True;

    // MPV events
    while (True) {
      mpv_event *mp_event = mpv_wait_event(mpv, 0);
      if (mp_event->event_id == MPV_EVENT_NONE)
        break;
      if (mp_event->event_id == MPV_EVENT_SHUTDOWN) {
        quit = True;
        break;
      }
      if (mp_event->event_id == MPV_EVENT_LOG_MESSAGE) {
        mpv_event_log_message *msg = mp_event->data;
        fprintf(stderr, "mpv: %s", msg->text);
        continue;
      }
      if (mp_event->event_id == MPV_EVENT_PROPERTY_CHANGE) {
        mpv_event_property *property = mp_event->data;
        if (strcmp(property->name, MPV_PROPERTY_TIME_REMAINING)) {
          double *data = property->data;
          if (data) {
            ping = True;
            // fprintf(stderr,"property: %s: %f\n", MPV_PROPERTY_TIME_REMAINING,
            // *data);
          }
        } else if (strcmp(property->name, MPV_PROPERTY_DEMUXER_CACHE_TIME)) {
          double *data = property->data;
          if (data) {
            // fprintf(stderr,"property: %s: %f\n",
            // MPV_PROPERTY_DEMUXER_CACHE_TIME, *data);

            if (*data > max_delay_s) {
              new_speed = 1.5;
            } else if (*data < min_delay_s) {
              new_speed = 1.0;
            }
          }
        }
        continue;
      }
      fprintf(stderr, "unhandled mpv event: %s\n",
              mpv_event_name(mp_event->event_id));
    }

    // MPV side effects
    if (new_speed && new_speed != mpv_state.speed) {
      if (mpv_set_property(mpv, "speed", MPV_FORMAT_DOUBLE, &new_speed) < 0)
        die("mpv failed set speed");
      mpv_state.speed = new_speed;
      mpv_state.speed_updated_at = time_now();
    }
    if (reload_file) {
      const char *cmd[] = {"loadfile", mpv_state.file, NULL};
      if (mpv_command(mpv, cmd) < 0)
        die("mpv failed to reload file");
    }
    if (reload_file || ping)
      mpv_state.pinged_at = time_now();

    clock_wait();
  }

  mpv_destroy(mpv);

  XCloseDisplay(display);

  return 0;
}
