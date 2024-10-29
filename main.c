#include "clock.h"
#include "layout.h"
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <argp.h>
#include <mpv/client.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef VERSION
#define VERSION "development"
#endif

void die(const char *msg) {
  fprintf(stderr, "%s\n", msg);
  exit(1);
}

int time_now() { return (int)time(NULL); }

// X11

int on_x11_error(Display *d, XErrorEvent *e) {
  fprintf(stderr, "xlib: %d\n", e->error_code);
  return 0;
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

mpv_handle *setup_mpv(Window window, char *hwdec) {
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

  return mpv;
}

void *shutdown_mpv(void *ptr) {
  mpv_handle *mpv = ptr;
  mpv_destroy(mpv);
  return NULL;
}

// CLI

#define MAX_FILES 32

struct Arguments {
  char *hwdec;
  int file_count;
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
        arguments->file_count++;
        break;
      }
    }
    break;
  }
  return 0;
}

struct State {
  int width;
  int height;
};

struct State init_state(Display *display, Window root_window) {
  XWindowAttributes a;
  if (XGetWindowAttributes(display, root_window, &a) < 0)
    die("failed to get root window size");

  struct State state = {
      .width = a.width,
      .height = a.height,
  };
  return state;
}

struct StreamState {
  Window window;
  mpv_handle *mpv;
  char *file;
  double speed;
  int speed_updated_at;
  int pinged_at;
};

int main(int argc, char *argv[]) {
  struct Arguments arguments = {};

  struct argp argp = {options, parser, 0, 0};
  argp_parse(&argp, argc, argv, 0, 0, &arguments);

  if (arguments.files[0] == NULL)
    die("no media files specified");

  XSetErrorHandler(on_x11_error);

  Display *display = XOpenDisplay(NULL);
  if (display == NULL)
    die("failed to open display");

  Window root_window = create_root_window(display);
  if (root_window == 0)
    die("failed to create window");

  Atom wm_delete_window = create_delete_window_atom(display, root_window);

  XMapWindow(display, root_window);

  struct State state = init_state(display, root_window);

  const int stream_count = arguments.file_count;

  struct StreamState stream_states[MAX_FILES] = {};

  {
    struct LayoutGrid layout =
        layout_grid_new(state.width, state.height, stream_count);

    for (int i = 0; i < stream_count; i++) {
      struct LayoutPane pane = layout_grid_pane(layout, i);
      Window window = XCreateSimpleWindow(display, root_window, pane.x, pane.y,
                                          pane.width, pane.height, 0, 0, 0);
      XMapWindow(display, window);

      mpv_handle *mpv = setup_mpv(window, arguments.hwdec);

      const char *cmd[] = {"loadfile", arguments.files[i], NULL};
      if (mpv_command(mpv, cmd) < 0)
        die("mpv failed to play file");

      struct StreamState stream_state = {
          .window = window,
          .mpv = mpv,
          .file = arguments.files[i],
          .speed = 1.0,
          .speed_updated_at = time_now(),
          .pinged_at = time_now(),
      };
      stream_states[i] = stream_state;
    }
  }

  clock_set_fps(60);

  int quit = False;
  while (!quit) {
    clock_start();

    Bool redraw = False;

    // X11 events
    while (XPending(display)) {
      XEvent event;
      XNextEvent(display, &event);
      switch (event.type) {
      case ClientMessage:
        if (event.xclient.data.l[0] == wm_delete_window) {
          quit = True;
        }
        break;
      case KeyPress: {
        fprintf(stderr, "KeyPress: %d\n", event.xkey.keycode);
        if (event.xkey.keycode == XKeysymToKeycode(display, XK_q)) {
          quit = True;
        }
        break;
      case ConfigureNotify:
        if (event.xconfigure.window == root_window) {
          state.width = event.xconfigure.width;
          state.height = event.xconfigure.height;
          redraw = True;
        }
        break;
      default:
        fprintf(stderr, "unhandled X11 event: %d\n", event.type);
      }
      }
    }

    for (int i = 0; i < arguments.file_count; i++) {
      double new_speed = 0;
      Bool ping = False;
      Bool reload_file = False;

      // Reset speed if stuck
      if (time_now() > stream_states[i].speed_updated_at + 5)
        new_speed = 1.0;

      // Reload locked up stream
      if (time_now() > (stream_states[i].pinged_at + timeout_s))
        reload_file = True;

      // MPV events
      while (True) {
        mpv_event *mp_event = mpv_wait_event(stream_states[i].mpv, 0);
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
              // fprintf(stderr,"property: %s: %f\n",
              // MPV_PROPERTY_TIME_REMAINING, *data);
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
      if (new_speed && new_speed != stream_states[i].speed) {
        int err = mpv_set_property(stream_states[i].mpv, "speed",
                                   MPV_FORMAT_DOUBLE, &new_speed);
        if (err < 0)
          fprintf(stderr, "failed to mpv set speed: error %d", err);
        else {
          stream_states[i].speed = new_speed;
          stream_states[i].speed_updated_at = time_now();
        }
      }
      if (reload_file) {
        const char *cmd[] = {"loadfile", stream_states[i].file, NULL};
        int err = mpv_command(stream_states[i].mpv, cmd) < 0;
        if (err < 0)
          fprintf(stderr, "failed to reload mpv file: error %d", err);
      }
      if (reload_file || ping)
        stream_states[i].pinged_at = time_now();
      if (redraw) {
        struct LayoutGrid layout =
            layout_grid_new(state.width, state.height, stream_count);
        for (int i = 0; i < stream_count; i++) {
          struct LayoutPane pane = layout_grid_pane(layout, i);
          XWindowChanges changes = {.x = pane.x,
                                    .y = pane.y,
                                    .width = pane.width,
                                    .height = pane.height};
          XConfigureWindow(display, stream_states[i].window,
                           CWX | CWY | CWWidth | CWHeight, &changes);
        }
      }
    }

    clock_wait();
  }

  // Concurrently shutdown all mpv handles
  pthread_t *threads = malloc(stream_count * sizeof(pthread_t));
  for (int i = 0; i < stream_count; i++) {
    pthread_create(&threads[i], NULL, shutdown_mpv, stream_states[i].mpv);
  }
  for (int i = 0; i < stream_count; i++) {
    pthread_join(threads[i], NULL);
  }
  free(threads);

  XCloseDisplay(display);

  return 0;
}
