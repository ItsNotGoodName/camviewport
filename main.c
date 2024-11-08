#include "./inih/ini.h"
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
#define VERSION "dev"
#endif

#define MAX_STREAMS 32

typedef struct {
  Window window;
  mpv_handle *mpv;
  char *file;
  double speed;
  int speed_updated_at;
  int pinged_at;
} StreamState;

typedef struct {
  Window window;
  int width;
  int height;
  Bool fullscreen;
  Window fullscreen_window;
  int stream_count;
  StreamState streams[MAX_STREAMS];
} State;

typedef struct {
  char *config_file;
  char *hwdec;
  int file_count;
  char *files[MAX_STREAMS];
} Config;

const static char *MPV_PROPERTY_DEMUXER_CACHE_TIME = "demuxer-cache-time";
const char *MPV_PROPERTY_TIME_REMAINING = "time-remaining";
const double MPV_MAX_DELAY_S = 0.5;
const double MPV_MIN_DISPLAY_S = 0.1;
const int MPV_TIMEOUT_S = 5;

static Display *display;
static Atom wm_delete_window;
static State *state;

static void die(const char *msg) {
  fprintf(stderr, "%s\n", msg);
  exit(1);
}

static int time_now() { return (int)time(NULL); }

static int on_x11_error(Display *d, XErrorEvent *e) {
  fprintf(stderr, "xlib: %d\n", e->error_code);
  return 0;
}

void setup() {
  XSetErrorHandler(on_x11_error);

  display = XOpenDisplay(NULL);
  if (display == NULL)
    die("failed to open display");

  Window root = XDefaultRootWindow(display);

  XWindowAttributes root_window_attribute;
  if (XGetWindowAttributes(display, root, &root_window_attribute) < 0)
    die("failed to get root window size");

  Window window =
      XCreateSimpleWindow(display, root, 0, 0, root_window_attribute.width,
                          root_window_attribute.height, 0, 0, 0);
  if (window == 0)
    die("failed to create window");

  XSelectInput(display, window, StructureNotifyMask | KeyPressMask);

  wm_delete_window = XInternAtom(display, "WM_DELETE_WINDOW", False);
  XSetWMProtocols(display, window, &wm_delete_window, 1);

  XMapWindow(display, window);

  XWindowAttributes window_attribute;
  if (XGetWindowAttributes(display, window, &window_attribute) < 0)
    die("failed to get window size");

  state = calloc(1, sizeof(State));
  state->window = window;
  state->width = window_attribute.width;
  state->height = window_attribute.height;
}

void *_destroy(void *ptr) {
  mpv_handle *mpv = ptr;
  mpv_destroy(mpv);
  return NULL;
}

void destory() {
  // Concurrently shutdown all mpv handles
  pthread_t *threads = malloc(state->stream_count * sizeof(pthread_t));
  for (int i = 0; i < state->stream_count; i++) {
    pthread_create(&threads[i], NULL, _destroy, state->streams[i].mpv);
  }
  for (int i = 0; i < state->stream_count; i++) {
    pthread_join(threads[i], NULL);
  }
  free(threads);

  XCloseDisplay(display);
}

mpv_handle *create_mpv(Window window, char *hwdec) {
  mpv_handle *mpv = mpv_create();
  if (mpv == NULL)
    die("mpv context failed");

  mpv_set_option(mpv, "wid", MPV_FORMAT_INT64, &window);
  mpv_set_option_string(mpv, "profile", "low-latency");
  mpv_set_option_string(mpv, "cache", "now");
  mpv_set_option_string(mpv, "input-cursor",
                        "no"); // FIXME: this causes the cursor disappears on a
                               // sub window when alt-tab is pressed, this only
                               // happens to sub window the cursor is hovering
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

void setup_streams(Config config) {
  struct LayoutGrid layout =
      layout_grid_new(state->width, state->height, config.file_count);

  for (int i = 0; i < config.file_count; i++) {
    struct LayoutPane pane = layout_grid_pane(layout, i);
    Window window = XCreateSimpleWindow(display, state->window, pane.x, pane.y,
                                        pane.width, pane.height, 0, 0, 0);
    XSelectInput(display, window, ButtonPressMask);
    XMapWindow(display, window);
    XSync(display, 0);

    mpv_handle *mpv = create_mpv(window, config.hwdec);

    const char *cmd[] = {"loadfile", config.files[i], NULL};
    if (mpv_command(mpv, cmd) < 0)
      die("mpv failed to play file");

    state->streams[i].window = window;
    state->streams[i].mpv = mpv;
    state->streams[i].file = config.files[i];
    state->streams[i].speed = 1.0;
    state->streams[i].speed_updated_at = time_now();
    state->streams[i].pinged_at = time_now();
  }

  state->stream_count = config.file_count;
}

static struct argp_option cli_options[] = {
    {"version", 'v', 0, 0, "Show version"},
    {"hwdec", 1, "HWDEC", 0, "Set hwdec mpv option"},
    {"config", 2, "FILENAME", 0, "Path to config file"},
    {0}};

static int config_cli_parser(int key, char *arg, struct argp_state *state) {
  Config *config = state->input;

  switch (key) {
  case 'v':
    printf("%s\n", VERSION);
    exit(0);
  case 1:
    config->hwdec = arg;
    break;
  case 2:
    config->config_file = arg;
  case ARGP_KEY_ARG:
    for (int i = 0; i < MAX_STREAMS; i++) {
      if (config->files[i] == NULL) {
        config->files[i] = arg;
        config->file_count++;
        break;
      }
    }
    break;
  }
  return 0;
}

static int config_file_parser(void *user, const char *section, const char *name,
                              const char *value) {
  Config *config = user;

#define MATCH(n) strcmp(name, n) == 0
  if (strcmp(section, "") == 0) {
    // Global
    if (MATCH("hwdec"))
      config->hwdec = strdup(value);
    else
      return 0;
  } else {
    // Stream
    if (MATCH("main")) {
      for (int i = 0; i < MAX_STREAMS; i++) {
        if (config->files[i] == NULL) {
          config->files[i] = strdup(value);
          config->file_count++;
          break;
        }
      }
    }
    return 0;
  }

  return 1;
}

void parse_config(int argc, char *argv[], Config *config) {
  struct argp argp = {cli_options, config_cli_parser, 0, 0};
  argp_parse(&argp, argc, argv, 0, 0, config);

  if (ini_parse(config->config_file, config_file_parser, config) < 0) {
    fprintf(stderr, "Failed to load '%s'\n", config->config_file);
    exit(1);
  }

  if (config->files[0] == NULL)
    die("No streams specified");
}

int main(int argc, char *argv[]) {
  Config config = {.config_file = "camviewport.ini"};

  parse_config(argc, argv, &config);

  setup();

  setup_streams(config);

  clock_set_fps(60);

  int quit = False;
  while (!quit) {
    clock_start();

    Bool redraw = False;
    Bool sync = False;

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
        if (event.xconfigure.window == state->window) {
          state->width = event.xconfigure.width;
          state->height = event.xconfigure.height;
          redraw = True;
        }
        break;
      case ButtonPress:
        fprintf(stderr, "ButtonPress: %u\n", event.xbutton.button);
        if (state->fullscreen) {
          state->fullscreen = False;
        } else {
          state->fullscreen = True;
          state->fullscreen_window = event.xbutton.window;
        }
        redraw = True;
        sync = True;
        break;
      default:
        fprintf(stderr, "unhandled X11 event: %d\n", event.type);
      }
      }
    }

    for (int i = 0; i < config.file_count; i++) {
      double new_speed = 0;
      Bool ping = False;
      Bool reload_file = False;

      // Reset speed if stuck
      if (time_now() > state->streams[i].speed_updated_at + 5)
        new_speed = 1.0;

      // Reload locked up stream
      if (time_now() > (state->streams[i].pinged_at + MPV_TIMEOUT_S))
        reload_file = True;

      // MPV events
      while (True) {
        mpv_event *mp_event = mpv_wait_event(state->streams[i].mpv, 0);
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

              if (*data > MPV_MAX_DELAY_S) {
                new_speed = 1.5;
              } else if (*data < MPV_MIN_DISPLAY_S) {
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
      if (new_speed && new_speed != state->streams[i].speed) {
        int err = mpv_set_property(state->streams[i].mpv, "speed",
                                   MPV_FORMAT_DOUBLE, &new_speed);
        if (err < 0)
          fprintf(stderr, "failed to mpv set speed: error %d", err);
        else {
          state->streams[i].speed = new_speed;
          state->streams[i].speed_updated_at = time_now();
        }
      }
      if (reload_file && (!state->fullscreen || state->streams[i].window ==
                                                    state->fullscreen_window)) {
        const char *cmd[] = {"loadfile", state->streams[i].file, NULL};
        int err = mpv_command(state->streams[i].mpv, cmd) < 0;
        if (err < 0)
          fprintf(stderr, "failed to reload mpv file: error %d", err);
      }
      if (reload_file || ping)
        state->streams[i].pinged_at = time_now();
      if (sync) {
        if (!state->fullscreen ||
            state->streams[i].window == state->fullscreen_window) {
          const char *cmd[] = {"loadfile", state->streams[i].file, NULL};
          int err = mpv_command(state->streams[i].mpv, cmd) < 0;
          if (err < 0)
            fprintf(stderr, "failed to play mpv file: error %d", err);
        } else {
          const char *cmd[] = {"stop", NULL};
          int err = mpv_command(state->streams[i].mpv, cmd) < 0;
          if (err < 0)
            fprintf(stderr, "failed to stop mpv file: error %d", err);
        }
      }
    }

    // X11 side effects
    if (redraw) {
      if (state->fullscreen) {
        for (int i = 0; i < state->stream_count; i++) {
          if (state->streams[i].window == state->fullscreen_window) {
            XWindowAttributes root;
            XGetWindowAttributes(display, state->window, &root);
            XWindowChanges changes = {
                .x = 0, .y = 0, .width = root.width, .height = root.height};
            XConfigureWindow(display, state->streams[i].window,
                             CWX | CWY | CWWidth | CWHeight, &changes);
            XMapWindow(display, state->streams[i].window);

          } else {
            XUnmapWindow(display, state->streams[i].window);
          }
        }
      } else {
        struct LayoutGrid layout =
            layout_grid_new(state->width, state->height, state->stream_count);
        for (int i = 0; i < state->stream_count; i++) {
          struct LayoutPane pane = layout_grid_pane(layout, i);
          XWindowChanges changes = {.x = pane.x,
                                    .y = pane.y,
                                    .width = pane.width,
                                    .height = pane.height};
          XConfigureWindow(display, state->streams[i].window,
                           CWX | CWY | CWWidth | CWHeight, &changes);
          XMapWindow(display, state->streams[i].window);
        }
      }
    }

    clock_wait();
  }

  destory();

  return 0;
}
