#include "config.h"
#include "./inih/ini.h"
#include "util.h"
#include <X11/Xlib.h>
#include <argp.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MPV_FLAG_PREFIX "mpv-"
#define MPV_FLAG_PREFIX_LEN 4
#define KEY_FLAG_PREFIX "key-"
#define KEY_FLAG_PREFIX_LEN 4

static void parse_mpv_flag(ConfigMpvFlags *config, const char *name,
                           const char *value) {
  if (config->count == MAX_MPV_FLAGS)
    die("too many flags");

  config->flags[config->count].name = strdup(&name[MPV_FLAG_PREFIX_LEN]);
  config->flags[config->count].data = strdup(value);
  config->count++;
}

static void add_key_sym(KeySym keys[MAX_KEYBINDINGS], KeySym key) {
  for (int i = 0; i < MAX_KEYBINDINGS; i++)
    if (keys[i] == 0) {
      keys[i] = key;
      break;
    }
}

static int handler(void *user, const char *section, const char *name,
                   const char *value) {
  Config *config = user;

#define SECTION(n) strcmp(section, n) == 0
#define MATCH(n) strcmp(name, n) == 0
#define MATCH_MPV strncmp(name, MPV_FLAG_PREFIX, MPV_FLAG_PREFIX_LEN) == 0
#define MATCH_KEY strncmp(name, KEY_FLAG_PREFIX, KEY_FLAG_PREFIX_LEN) == 0
#define VALUE(n) strcmp(value, n) == 0

  if (SECTION("")) {
    if (MATCH_MPV)
      parse_mpv_flag(&config->mpv_flags, name, value);
    else if (MATCH_KEY) {
      KeySym key_sym = XStringToKeysym(&name[KEY_FLAG_PREFIX_LEN]);
      if (VALUE("quit"))
        add_key_sym(config->key_map.quit, key_sym);
      else if (VALUE("home"))
        add_key_sym(config->key_map.home, key_sym);
      else if (VALUE("next"))
        add_key_sym(config->key_map.next, key_sym);
      else if (VALUE("previous"))
        add_key_sym(config->key_map.previous, key_sym);
      else if (VALUE("reload"))
        add_key_sym(config->key_map.reload, key_sym);
    } else if (MATCH("layout"))
      config->layout_file = strdup(value);
    else
      return 0;
    return 1;
  }

  // Get index
  int index = -1;
  for (int i = 0; i < config->stream_count; i++)
    if (strcmp(config->streams[i].name, section) == 0) {
      // Found
      index = i;
      break;
    }

  if (index == -1) {
    // Create
    if (config->stream_count == MAX_STREAMS)
      die("too many streams");
    index = config->stream_count;

    config->streams[index].name = strdup(section);
    config->stream_count++;
  }

  if (MATCH("main"))
    config->streams[index].main = strdup(value);
  else if (MATCH("sub"))
    config->streams[index].sub = strdup(value);
  else if (MATCH_MPV)
    parse_mpv_flag(&config->streams[index].mpv_flags, name, value);
  else
    return 0;

  return 1;
}

static struct argp_option options[] = {
    {"version", 'v', 0, 0, "Show version"},
    {"config", 'c', "FILENAME", 0, "Path to config file"},
    {"layout", 'l', "LAYOUT", 0, "Path to layout file"},
    {0}};

static int parser(int key, char *arg, struct argp_state *state) {
  Config *config = state->input;

  switch (key) {
  case 'v':
    config->show_version = 1;
    break;
  case 'c':
    config->config_file = arg;
    break;
  case 'l':
    config->layout_file = arg;
    break;
  }
  return 0;
}

void config_parse(Config *config, int argc, char *argv[]) {
  struct argp argp = {options, parser, 0, 0};
  argp_parse(&argp, argc, argv, 0, 0, config);

  if (access(config->config_file, F_OK) == 0 &&
      ini_parse(config->config_file, handler, config) < 0) {
    fprintf(stderr, "Failed to load '%s'\n", config->config_file);
    exit(1);
  }
}
