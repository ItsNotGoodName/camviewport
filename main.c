#include "clock.h"
#include "config.h"
#include "layout.h"
#include "player.h"
#include "util.h"
#include <X11/X.h>
#include <X11/Xlib.h>
#include <mpv/client.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
  COMMAND_SYNC_X11 = 0x00000001,
  COMMAND_SYNC_MPV = 0x00000010,
} Command;

typedef enum {
  VIEW_GRID,
  VIEW_FULLSCREEN,
  VIEW_LAYOUT,
} View;

typedef struct {
  KeyCode quit;
  KeyCode home;
  KeyCode next;
  KeyCode previous;
  KeyCode reload;
} KeyMap;

typedef struct {
  Window window;
  mpv_handle *mpv;
  char *main;
  char *sub;
  double speed;
  int speed_updated_at;
  int pinged_at;
} StreamState;

typedef struct {
  Window window;
  int width;
  int height;
  View view;
  View mode;
  Window fullscreen_window;
  char *layout_file_path;
  LayoutFile layout_file;
  int stream_count;
  StreamState streams[MAX_STREAMS];
  KeyMap key_map;
} State;

const static char *MPV_PROPERTY_DEMUXER_CACHE_TIME = "demuxer-cache-time";
const char *MPV_PROPERTY_TIME_REMAINING = "time-remaining";
const double MPV_MAX_DELAY_SEC = 0.5;
const double MPV_MIN_DISPLAY_SEC = 0.1;
const int MPV_TIMEOUT_SEC = 5;

static int time_now() { return (int)time(NULL); }

static int on_x11_error(Display *d, XErrorEvent *e) {
  fprintf(stderr, "xlib: %d\n", e->error_code);
  return 0;
}

static Display *display;
static Atom wm_delete_window;
static State *state;

void setup() {
  XSetErrorHandler(on_x11_error);

  display = XOpenDisplay(NULL);
  if (display == NULL)
    die("failed to open display");

  Window root = XDefaultRootWindow(display);
  XSelectInput(display, root, StructureNotifyMask);

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

Command update_dim(int width, int height) {
  state->width = width;
  state->height = height;
  return COMMAND_SYNC_X11;
}

Command toggle_fullscreen(Window window) {
  if (state->view == VIEW_FULLSCREEN) {
    state->view = state->mode;
  } else if (window) {
    state->view = VIEW_FULLSCREEN;
    state->fullscreen_window = window;
  } else if (state->fullscreen_window) {
    state->view = VIEW_FULLSCREEN;
  } else if (state->stream_count > 0) {
    state->view = VIEW_FULLSCREEN;
    state->fullscreen_window = state->streams[0].window;
  }
  return COMMAND_SYNC_X11 | COMMAND_SYNC_MPV;
}

Command go_next() {
  int index = state->stream_count - 1;

  if (state->view == VIEW_FULLSCREEN) {
    for (int i = 0; i < state->stream_count; i++) {
      if (state->streams[i].window == state->fullscreen_window) {
        index = i;
        break;
      }
    }
  }

  state->view = VIEW_FULLSCREEN;
  state->fullscreen_window =
      state->streams[(index + 1) % state->stream_count].window;
  return COMMAND_SYNC_X11 | COMMAND_SYNC_MPV;
}

Command go_previous() {
  int index = 0;

  if (state->view == VIEW_FULLSCREEN) {
    for (int i = 0; i < state->stream_count; i++) {
      if (state->streams[i].window == state->fullscreen_window) {
        index = i;
        break;
      }
    }
  }

  state->view = VIEW_FULLSCREEN;
  state->fullscreen_window =
      state->streams[index - 1 >= 0 ? index - 1 : state->stream_count - 1]
          .window;
  return COMMAND_SYNC_X11 | COMMAND_SYNC_MPV;
}

void sync_mpv(int index) {
  switch (state->view) {
  case VIEW_FULLSCREEN: {
    if (state->fullscreen_window == state->streams[index].window) {
      player_loadfile(state->streams[index].mpv, state->streams[index].main);
    } else {
      player_stop(state->streams[index].mpv);
    }
    break;
  }
  case VIEW_GRID: {
    char *stream = state->streams[index].sub;
    if (state->stream_count == 1)
      stream = state->streams[index].main;

    player_loadfile(state->streams[index].mpv, stream);
    break;
  }
  case VIEW_LAYOUT: {
    player_loadfile(state->streams[index].mpv, state->streams[index].sub);
    break;
  }
  }
}

void sync_x11() {
  switch (state->view) {
  case VIEW_FULLSCREEN: {
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
    break;
  }
  case VIEW_GRID: {
    LayoutGrid layout =
        layout_grid_new(state->width, state->height, state->stream_count);
    for (int i = 0; i < state->stream_count; i++) {
      LayoutWindow pane = layout_grid_window(layout, i);
      XWindowChanges changes = {
          .x = pane.x,
          .y = pane.y,
          .width = pane.width,
          .height = pane.height,
      };
      XConfigureWindow(display, state->streams[i].window,
                       CWX | CWY | CWWidth | CWHeight, &changes);
      XMapWindow(display, state->streams[i].window);
    }
    break;
  }
  case VIEW_LAYOUT: {
    int visible = MIN(state->stream_count, state->layout_file.pane_count);
    for (int i = 0; i < visible; i++) {
      XWindowChanges changes = {
          .x = state->layout_file.panes[i].x * state->width,
          .y = state->layout_file.panes[i].y * state->height,
          .width = state->layout_file.panes[i].width * state->width,
          .height = state->layout_file.panes[i].height * state->height,
      };
      XConfigureWindow(display, state->streams[i].window,
                       CWX | CWY | CWWidth | CWHeight, &changes);
      XMapWindow(display, state->streams[i].window);
    }

    for (int i = visible; i < state->stream_count; i++)
      XUnmapWindow(display, state->streams[i].window);

    break;
  }
  }
}

Command reload_layout_file() {
  if (!state->layout_file_path)
    return 0;
  if (layout_file_reload(&state->layout_file, state->layout_file_path) < 0) {
    fprintf(stderr, "Failed to load layout '%s'\n", state->layout_file_path);
    return 0;
  }
  return COMMAND_SYNC_X11;
}

void load_config(Config config) {
  // Load key map
  state->key_map.quit = XKeysymToKeycode(display, config.key_map.quit);
  state->key_map.home = XKeysymToKeycode(display, config.key_map.home);
  state->key_map.next = XKeysymToKeycode(display, config.key_map.next);
  state->key_map.previous = XKeysymToKeycode(display, config.key_map.previous);
  state->key_map.reload = XKeysymToKeycode(display, config.key_map.reload);

  // Load layout
  if (config.layout_file) {
    state->layout_file_path = config.layout_file;
    state->mode = VIEW_LAYOUT;
    state->view = VIEW_LAYOUT;
    if (layout_file_reload(&state->layout_file, config.layout_file) < 0) {
      fprintf(stderr, "Failed to load layout '%s'\n", config.layout_file);
      exit(1);
    }

    fprintf(stderr, "loading layout file: %s\n", state->layout_file.name);
  }

  // Load streams
  state->stream_count = config.stream_count;
  for (int i = 0; i < state->stream_count; i++) {
    Window window =
        XCreateSimpleWindow(display, state->window, 0, 0, 1, 1, 0, 0, 0);
    XSelectInput(display, window, ButtonPressMask);
    XSync(display, 0);

    mpv_handle *mpv = mpv_create();
    if (mpv == NULL)
      die("mpv context failed");

    mpv_set_option(mpv, "wid", MPV_FORMAT_INT64, &window);
    // mpv_set_option_string(mpv, "idle", "yes");
    // mpv_set_option_string(mpv, "force-window", "yes");
    mpv_set_option_string(mpv, "profile", "low-latency");
    mpv_set_option_string(mpv, "cache", "now");
    mpv_set_option_string(
        mpv, "input-cursor",
        "no"); // FIXME: this causes the cursor disappears on a
               // sub window when alt-tab is pressed, this only
               // happens to sub window the cursor is hovering
    mpv_set_option_string(mpv, "ao",
                          "null"); // FIXME: audio other than null causes
                                   // crashes when started with startx
    for (int j = 0; j < config.mpv_flags.count; j++)
      mpv_set_option_string(mpv, config.mpv_flags.flags[j].name,
                            config.mpv_flags.flags[j].data);
    for (int j = 0; j < config.streams[i].mpv_flags.count; j++)
      mpv_set_option_string(mpv, config.streams[i].mpv_flags.flags[j].name,
                            config.streams[i].mpv_flags.flags[j].data);

    mpv_observe_property(mpv, 0, MPV_PROPERTY_TIME_REMAINING,
                         MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv, 0, MPV_PROPERTY_DEMUXER_CACHE_TIME,
                         MPV_FORMAT_DOUBLE);

    if (mpv_initialize(mpv) < 0)
      die("mpv init failed");

    mpv_request_log_messages(mpv, "info");

    state->streams[i].window = window;
    state->streams[i].mpv = mpv;
    state->streams[i].main = config.streams[i].main;
    state->streams[i].sub = config.streams[i].sub;
    state->streams[i].speed = 1.0;
    state->streams[i].speed_updated_at = time_now();
    state->streams[i].pinged_at = time_now();
  }
}

void run() {
  for (int i = 0; i < state->stream_count; i++)
    sync_mpv(i);

  sync_x11();

  clock_set_fps(60);

  while (True) {
    clock_start();

    Command command = 0;

    // X11 events
    while (XPending(display)) {
      XEvent event;
      XNextEvent(display, &event);
      switch (event.type) {
      case ClientMessage:
        if (event.xclient.data.l[0] == wm_delete_window)
          return;
        break;
      case KeyPress: {
        fprintf(stderr, "KeyPress: %d\n", event.xkey.keycode);
        if (event.xkey.keycode == state->key_map.quit) {
          return;
        } else if (event.xkey.keycode == state->key_map.home) {
          command |= toggle_fullscreen(0);
        } else if (event.xkey.keycode == state->key_map.next) {
          command |= go_next();
        } else if (event.xkey.keycode == state->key_map.previous) {
          command |= go_previous();
        } else if (event.xkey.keycode == state->key_map.reload) {
          command |= reload_layout_file();
        }
        break;
      case ConfigureNotify:
        if (event.xconfigure.window == state->window) {
          command |=
              update_dim(event.xconfigure.width, event.xconfigure.height);
        } else {
          // Assume this is the root window
          XWindowChanges changes = {.x = 0,
                                    .y = 0,
                                    .width = event.xconfigure.width,
                                    .height = event.xconfigure.height};
          XConfigureWindow(display, state->window,
                           CWX | CWY | CWWidth | CWHeight, &changes);
        }
        break;
      case ButtonPress:
        fprintf(stderr, "ButtonPress: %u\n", event.xbutton.button);
        command |= toggle_fullscreen(event.xbutton.window);
        break;
      default:
        fprintf(stderr, "unhandled X11 event: %d\n", event.type);
      }
      }
    }

    for (int stream_index = 0; stream_index < state->stream_count;
         stream_index++) {
      double new_speed = 0;
      Bool ping = False;
      Bool reload_file = False;

      // Reset speed if stuck
      if (time_now() > state->streams[stream_index].speed_updated_at + 5)
        new_speed = 1.0;

      // Reload locked up stream
      if (time_now() >
          (state->streams[stream_index].pinged_at + MPV_TIMEOUT_SEC))
        reload_file = True;

      // mpv events
      while (True) {
        mpv_event *mp_event =
            mpv_wait_event(state->streams[stream_index].mpv, 0);
        if (mp_event->event_id == MPV_EVENT_NONE)
          break;
        if (mp_event->event_id == MPV_EVENT_SHUTDOWN)
          return;
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

              if (*data > MPV_MAX_DELAY_SEC) {
                new_speed = 1.5;
              } else if (*data < MPV_MIN_DISPLAY_SEC) {
                new_speed = 1.0;
              }
            }
          }
          continue;
        }
        fprintf(stderr, "unhandled mpv event: %s\n",
                mpv_event_name(mp_event->event_id));
      }

      // mpv side effects
      if (new_speed && new_speed != state->streams[stream_index].speed) {
        int err = mpv_set_property(state->streams[stream_index].mpv, "speed",
                                   MPV_FORMAT_DOUBLE, &new_speed);
        if (err < 0)
          fprintf(stderr, "failed to mpv set speed: error %d", err);
        else {
          state->streams[stream_index].speed = new_speed;
          state->streams[stream_index].speed_updated_at = time_now();
        }
      }
      if (reload_file || command & COMMAND_SYNC_MPV)
        sync_mpv(stream_index);
      if (reload_file || ping)
        state->streams[stream_index].pinged_at = time_now();
    }

    // X11 side effects
    if (command & COMMAND_SYNC_X11)
      sync_x11();

    clock_wait();
  }
}

int main(int argc, char *argv[]) {
  Config config = {
      .config_file = "camviewport.ini",
      .key_map =
          {
              .quit = XStringToKeysym("q"),
              .home = XStringToKeysym("space"),
              .next = XStringToKeysym("l"),
              .previous = XStringToKeysym("h"),
              .reload = XStringToKeysym("r"),
          },
  };

  config_parse(&config, argc, argv);

  if (config.show_version) {
    printf("%s\n", VERSION);
    exit(0);
  }

  setup();

  load_config(config);

  run();

  destory();
}
