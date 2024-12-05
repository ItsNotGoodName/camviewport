#include "clock.h"
#include "config.h"
#include "layout.h"
#include "util.h"
#include <X11/Xlib.h>
#include <mpv/client.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef enum {
  COMMAND_SYNC_X11 = 0x00000001,
  COMMAND_SYNC_MPV = 0x00000010,
  COMMAND_SYNC_SPEED = 0x00000100,
} Command;

typedef enum {
  VIEW_GRID,
  VIEW_FULLSCREEN,
  VIEW_LAYOUT,
} View;

typedef struct {
  KeyCode quit[MAX_KEYBINDINGS];
  KeyCode home[MAX_KEYBINDINGS];
  KeyCode next[MAX_KEYBINDINGS];
  KeyCode previous[MAX_KEYBINDINGS];
  KeyCode reload[MAX_KEYBINDINGS];
} KeyMap;

typedef struct {
  char *name;
  Window window;
  mpv_handle *mpv;
  char *main;
  char *sub;
  double speed;
  int speed_updated_at;
  int pinged_at;
  ConfigMpvFlags main_mpv_flags;
  ConfigMpvFlags sub_mpv_flags;
} StreamState;

typedef struct {
  Window window;
  int width;
  int height;
  View view;
  View mode;
  Window fullscreen_window;
  const char *layout_file_path;
  LayoutFile layout_file;
  int stream_count;
  StreamState streams[MAX_STREAMS];
  ConfigMpvFlags main_mpv_flags;
  ConfigMpvFlags sub_mpv_flags;
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
  for (int i = 0; i < state->stream_count; i++)
    pthread_create(&threads[i], NULL, _destroy, state->streams[i].mpv);
  for (int i = 0; i < state->stream_count; i++)
    pthread_join(threads[i], NULL);
  free(threads);

  XCloseDisplay(display);
}

void player_loadfile(int stream_i, char *stream) {
  const char *cmd[] = {"loadfile", stream, NULL};
  int err = mpv_command(state->streams[stream_i].mpv, cmd) < 0;
  if (err < 0)
    fprintf(stderr, "%s: failed to play file: %d\n",
            state->streams[stream_i].name, err);
}

void player_stop(int stream_i) {
  const char *cmd[] = {"stop", NULL};
  int err = mpv_command(state->streams[stream_i].mpv, cmd) < 0;
  if (err < 0)
    fprintf(stderr, "%s: failed to stop file: %d\n",
            state->streams[stream_i].name, err);
}

void player_set_speed(int stream_i, double speed) {
  int err = mpv_set_property(state->streams[stream_i].mpv, "speed",
                             MPV_FORMAT_DOUBLE, &speed);
  if (err < 0)
    fprintf(stderr, "%s: failed to set speed: %d\n",
            state->streams[stream_i].name, err);
}

Command update_size(int width, int height) {
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

  if (state->view == VIEW_FULLSCREEN)
    for (int i = 0; i < state->stream_count; i++)
      if (state->streams[i].window == state->fullscreen_window) {
        index = i;
        break;
      }

  state->view = VIEW_FULLSCREEN;
  state->fullscreen_window =
      state->streams[(index + 1) % state->stream_count].window;
  return COMMAND_SYNC_X11 | COMMAND_SYNC_MPV;
}

Command go_previous() {
  int index = 0;

  if (state->view == VIEW_FULLSCREEN)
    for (int i = 0; i < state->stream_count; i++)
      if (state->streams[i].window == state->fullscreen_window) {
        index = i;
        break;
      }

  state->view = VIEW_FULLSCREEN;
  state->fullscreen_window =
      state->streams[index - 1 >= 0 ? index - 1 : state->stream_count - 1]
          .window;
  return COMMAND_SYNC_X11 | COMMAND_SYNC_MPV;
}

int is_mpv_playing(int index) {
  switch (state->view) {
  case VIEW_FULLSCREEN:
    return state->fullscreen_window == state->streams[index].window;
  case VIEW_GRID:
  case VIEW_LAYOUT:
    return 1;
  default:
    return 0;
  }
}

void apply_mpv_flags_property(mpv_handle *mpv, ConfigMpvFlags f) {
  for (int i = 0; i < f.count; i++)
    mpv_set_property_string(mpv, f.flags[i].name, f.flags[i].data);
}

void apply_mpv_flags_option(mpv_handle *mpv, ConfigMpvFlags f) {
  for (int i = 0; i < f.count; i++)
    mpv_set_option_string(mpv, f.flags[i].name, f.flags[i].data);
}

void sync_mpv(int index) {
  // printf("DEBUG: syncing mpv: %d\n", index);
  mpv_handle *mpv = state->streams[index].mpv;
  switch (state->view) {
  case VIEW_FULLSCREEN: {
    if (state->fullscreen_window == state->streams[index].window) {
      player_loadfile(index, state->streams[index].main);
      apply_mpv_flags_property(mpv, state->main_mpv_flags);
      apply_mpv_flags_property(mpv, state->streams[index].main_mpv_flags);
    } else {
      player_stop(index);
    }

    break;
  }
  case VIEW_GRID: {
    if (state->stream_count == 1) {
      player_loadfile(index, state->streams[index].main);
      apply_mpv_flags_property(mpv, state->main_mpv_flags);
      apply_mpv_flags_property(mpv, state->streams[index].main_mpv_flags);
    } else {
      player_loadfile(index, state->streams[index].sub);
      apply_mpv_flags_property(mpv, state->sub_mpv_flags);
      apply_mpv_flags_property(mpv, state->streams[index].sub_mpv_flags);
    }

    break;
  }
  case VIEW_LAYOUT: {
    player_loadfile(index, state->streams[index].sub);
    apply_mpv_flags_property(mpv, state->sub_mpv_flags);
    apply_mpv_flags_property(mpv, state->streams[index].sub_mpv_flags);
    break;
  }
  }
}

void sync_mpv_speed(int index) {
  // printf("DEBUG: syncing mpv speed: %d\n", index);
  player_set_speed(index, state->streams[index].speed);
}

void sync_x11() {
  // printf("DEBUG: syncing x11\n");
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

Command update_mpv_speed(int stream_i, double new_speed) {
  if (state->streams[stream_i].speed == new_speed)
    return 0;
  fprintf(stderr, "%s: updating speed: %f -> %f\n",
          state->streams[stream_i].name, state->streams[stream_i].speed,
          new_speed);
  state->streams[stream_i].speed = new_speed;
  state->streams[stream_i].speed_updated_at = time_now();
  return COMMAND_SYNC_SPEED;
}

Command reload_mpv(int stream_i) {
  state->streams[stream_i].pinged_at = time_now();
  return COMMAND_SYNC_MPV;
}

Command reload_layout_file() {
  if (!state->layout_file_path)
    return 0;
  if (layout_file_reload(&state->layout_file, state->layout_file_path) < 0) {
    fprintf(stderr, "failed to load layout '%s'\n", state->layout_file_path);
    return 0;
  }
  fprintf(stderr, "reloaded layout file: %s\n", state->layout_file_path);
  return COMMAND_SYNC_X11;
}

void load_config(Config config) {
  // Load key map
  for (int i = 0; i < MAX_KEYBINDINGS; i++) {
    state->key_map.quit[i] = XKeysymToKeycode(display, config.key_map.quit[i]);
    state->key_map.home[i] = XKeysymToKeycode(display, config.key_map.home[i]);
    state->key_map.next[i] = XKeysymToKeycode(display, config.key_map.next[i]);
    state->key_map.previous[i] =
        XKeysymToKeycode(display, config.key_map.previous[i]);
    state->key_map.reload[i] =
        XKeysymToKeycode(display, config.key_map.reload[i]);
  }

  // Load layout
  if (config.layout_file) {
    state->layout_file_path = config.layout_file;
    state->mode = VIEW_LAYOUT;
    state->view = VIEW_LAYOUT;
    if (layout_file_reload(&state->layout_file, config.layout_file) < 0) {
      fprintf(stderr, "failed to load layout '%s'\n", config.layout_file);
      exit(1);
    }

    fprintf(stderr, "loading layout file: %s\n", state->layout_file.name);
  }

  // Load streams
  state->stream_count = config.stream_count;
  state->main_mpv_flags = config.main_mpv_flags;
  state->sub_mpv_flags = config.sub_mpv_flags;
  for (int stream_i = 0; stream_i < config.stream_count; stream_i++) {
    Window window =
        XCreateSimpleWindow(display, state->window, 0, 0, 1, 1, 0, 0, 0);
    XSelectInput(display, window, ButtonPressMask);
    XSync(display, 0);

    mpv_handle *mpv = mpv_create();
    if (mpv == NULL)
      die("failed to create mpv context");

    mpv_set_option(mpv, "wid", MPV_FORMAT_INT64, &window);
    // mpv_set_option_string(mpv, "idle", "yes");
    // mpv_set_option_string(mpv, "force-window", "yes");
    mpv_set_option_string(mpv, "profile", "low-latency");
    mpv_set_option_string(mpv, "cache", "now");
    mpv_set_option_string(
        mpv, "input-cursor",
        "no"); // FIXME: this causes the cursor disappears on a
               // sub window when alt-tab is pressed, it only
               // happens to sub window the cursor is hovering
    mpv_set_option_string(mpv, "ao",
                          "null"); // FIXME: audio other than null causes
                                   // crashes when started with startx

    apply_mpv_flags_option(mpv, config.mpv_flags);
    apply_mpv_flags_option(mpv, config.streams[stream_i].mpv_flags);

    mpv_observe_property(mpv, 0, MPV_PROPERTY_TIME_REMAINING,
                         MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv, 0, MPV_PROPERTY_DEMUXER_CACHE_TIME,
                         MPV_FORMAT_DOUBLE);

    if (mpv_initialize(mpv) < 0)
      die("failed to init mpv");

    mpv_request_log_messages(mpv, "info");

    state->streams[stream_i].name = config.streams[stream_i].name;
    state->streams[stream_i].window = window;
    state->streams[stream_i].mpv = mpv;
    state->streams[stream_i].main = config.streams[stream_i].main == 0
                                        ? config.streams[stream_i].sub
                                        : config.streams[stream_i].main;
    state->streams[stream_i].sub = config.streams[stream_i].sub == 0
                                       ? config.streams[stream_i].main
                                       : config.streams[stream_i].sub;
    state->streams[stream_i].speed = 1.0;
    state->streams[stream_i].speed_updated_at = time_now();
    state->streams[stream_i].pinged_at = time_now();
    state->streams[stream_i].main_mpv_flags =
        config.streams[stream_i].main_mpv_flags;
    state->streams[stream_i].sub_mpv_flags =
        config.streams[stream_i].sub_mpv_flags;
  }
}

void run() {
  sync_x11();

  for (int i = 0; i < state->stream_count; i++)
    sync_mpv(i);

  clock_set_fps(60);

  while (True) {
    clock_start();

    Command root_command = 0;

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
        // fprintf(stderr, "KeyPress: %d\n", event.xkey.keycode);
        for (int key_i = 0; key_i < MAX_KEYBINDINGS; key_i++) {
          if (event.xkey.keycode == state->key_map.quit[key_i]) {
            return;
          } else if (event.xkey.keycode == state->key_map.home[key_i]) {
            root_command |= toggle_fullscreen(0);
          } else if (event.xkey.keycode == state->key_map.next[key_i]) {
            root_command |= go_next();
          } else if (event.xkey.keycode == state->key_map.previous[key_i]) {
            root_command |= go_previous();
          } else if (event.xkey.keycode == state->key_map.reload[key_i]) {
            root_command |= reload_layout_file();
          } else {
            continue;
          }
          break;
        }
        break;
      case ConfigureNotify:
        if (event.xconfigure.window == state->window) {
          root_command |=
              update_size(event.xconfigure.width, event.xconfigure.height);
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
        // fprintf(stderr, "ButtonPress: %u\n", event.xbutton.button);
        root_command |= toggle_fullscreen(event.xbutton.window);
        break;
        // default:
        //   fprintf(stderr, "unhandled X11 event: %d\n", event.type);
      }
      }
    }

    for (int stream_i = 0; stream_i < state->stream_count; stream_i++) {
      Command sub_command = root_command;

      // Reload locked up stream
      if (is_mpv_playing(stream_i) &&
          time_now() > (state->streams[stream_i].pinged_at + MPV_TIMEOUT_SEC))
        sub_command |= reload_mpv(stream_i);

      // Reset speed if stuck
      if (time_now() > state->streams[stream_i].speed_updated_at + 5)
        sub_command |= update_mpv_speed(stream_i, 1.0);

      // mpv events
      while (True) {
        mpv_event *mp_event = mpv_wait_event(state->streams[stream_i].mpv, 0);
        if (mp_event->event_id == MPV_EVENT_NONE)
          break;
        if (mp_event->event_id == MPV_EVENT_SHUTDOWN)
          return;
        if (mp_event->event_id == MPV_EVENT_LOG_MESSAGE) {
          mpv_event_log_message *msg = mp_event->data;
          fprintf(stderr, "%s: %s", state->streams[stream_i].name, msg->text);
          continue;
        }
        if (mp_event->event_id == MPV_EVENT_PROPERTY_CHANGE) {
          mpv_event_property *property = mp_event->data;
          if (strcmp(property->name, MPV_PROPERTY_TIME_REMAINING)) {
            double *data = property->data;
            if (data) {
              state->streams[stream_i].pinged_at = time_now();
              // fprintf(stderr, "property: %s: %f\n",
              // MPV_PROPERTY_TIME_REMAINING,
              //         *data);
            }
          } else if (strcmp(property->name, MPV_PROPERTY_DEMUXER_CACHE_TIME)) {
            double *data = property->data;
            if (data) {
              // fprintf(stderr, "property: %s: %f\n",
              //         MPV_PROPERTY_DEMUXER_CACHE_TIME, *data);

              if (*data > MPV_MAX_DELAY_SEC) {
                sub_command |= update_mpv_speed(stream_i, 1.5);
              } else if (*data < MPV_MIN_DISPLAY_SEC) {
                sub_command |= update_mpv_speed(stream_i, 1.0);
              }
            }
          }
          continue;
        }
        // fprintf(stderr, "%s: unhandled mpv event: %s\n",
        //         state->streams[stream_i].name,
        //         mpv_event_name(mp_event->event_id));
      }

      // mpv side effects
      if (sub_command & COMMAND_SYNC_MPV)
        sync_mpv(stream_i);
      if (sub_command & COMMAND_SYNC_SPEED)
        sync_mpv_speed(stream_i);
    }

    // X11 side effects
    if (root_command & COMMAND_SYNC_X11)
      sync_x11();

    clock_wait();
  }
}

int main(int argc, const char *argv[]) {
  Config config = {
      .config_file = "camviewport.ini",
      .key_map =
          {
              .quit[MAX_KEYBINDINGS - 1] = XStringToKeysym("q"),
              .home[MAX_KEYBINDINGS - 1] = XStringToKeysym("space"),
              .next[MAX_KEYBINDINGS - 1] = XStringToKeysym("l"),
              .previous[MAX_KEYBINDINGS - 1] = XStringToKeysym("h"),
              .reload[MAX_KEYBINDINGS - 1] = XStringToKeysym("r"),
          },
  };

  config_parse(&config, argc, argv);

  setup();

  load_config(config);

  run();

  destory();
}
