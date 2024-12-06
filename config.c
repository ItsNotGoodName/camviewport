#include "config.h"
#include "./flag/flag.h"
#include "./inih/ini.h"
#include "util.h"
#include <X11/Xlib.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

const char *MPV_FLAG_PREFIX = "mpv-";
const int MPV_FLAG_PREFIX_LEN = 4;
const char *MAIN_MPV_FLAG_PREFIX = "main-mpv-";
const int MAIN_MPV_FLAG_PREFIX_LEN = 9;
const char *SUB_MPV_FLAG_PREFIX = "sub-mpv-";
const int SUB_MPV_FLAG_PREFIX_LEN = 8;
const char *KEY_FLAG_PREFIX = "key-";
const int KEY_FLAG_PREFIX_LEN = 4;

static void parse_mpv_flag(ConfigMpvFlags *config, const char *name, const char *value, int prefix_len) {
  if (config->count == MAX_MPV_FLAGS)
    die("too many mpv flags");

  config->flags[config->count].name = strdup(&name[prefix_len]);
  config->flags[config->count].data = strdup(value);
  config->count++;
}

static void append_key_sym(KeySym keys[MAX_KEYBINDINGS], KeySym key) {
  for (int i = 0; i < MAX_KEYBINDINGS; i++)
    if (keys[i] == 0) {
      keys[i] = key;
      break;
    }
}

static int handler(void *user, const char *section, const char *name, const char *value) {
  Config *config = user;

#define SECTION(n) strcmp(section, n) == 0
#define MATCH(n) strcmp(name, n) == 0
#define MATCH_MPV strncmp(name, MPV_FLAG_PREFIX, MPV_FLAG_PREFIX_LEN) == 0
#define MATCH_MAIN_MPV strncmp(name, MAIN_MPV_FLAG_PREFIX, MAIN_MPV_FLAG_PREFIX_LEN) == 0
#define MATCH_SUB_MPV strncmp(name, SUB_MPV_FLAG_PREFIX, SUB_MPV_FLAG_PREFIX_LEN) == 0
#define MATCH_KEY strncmp(name, KEY_FLAG_PREFIX, KEY_FLAG_PREFIX_LEN) == 0
#define VALUE(n) strcmp(value, n) == 0

  if (SECTION("")) {
    if (MATCH_MPV)
      parse_mpv_flag(&config->mpv_flags, name, value, MPV_FLAG_PREFIX_LEN);
    else if (MATCH_MAIN_MPV)
      parse_mpv_flag(&config->main_mpv_flags, name, value, MAIN_MPV_FLAG_PREFIX_LEN);
    else if (MATCH_SUB_MPV)
      parse_mpv_flag(&config->sub_mpv_flags, name, value, SUB_MPV_FLAG_PREFIX_LEN);
    else if (MATCH_KEY) {
      KeySym key_sym = XStringToKeysym(&name[KEY_FLAG_PREFIX_LEN]);
      if (VALUE("quit"))
        append_key_sym(config->key_map.quit, key_sym);
      else if (VALUE("home"))
        append_key_sym(config->key_map.home, key_sym);
      else if (VALUE("next"))
        append_key_sym(config->key_map.next, key_sym);
      else if (VALUE("previous"))
        append_key_sym(config->key_map.previous, key_sym);
      else if (VALUE("reload"))
        append_key_sym(config->key_map.reload, key_sym);
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
    parse_mpv_flag(&config->streams[index].mpv_flags, name, value, MPV_FLAG_PREFIX_LEN);
  else if (MATCH_MAIN_MPV)
    parse_mpv_flag(&config->streams[index].main_mpv_flags, name, value, MAIN_MPV_FLAG_PREFIX_LEN);
  else if (MATCH_SUB_MPV)
    parse_mpv_flag(&config->streams[index].sub_mpv_flags, name, value, SUB_MPV_FLAG_PREFIX_LEN);
  else
    return 0;

  return 1;
}

void config_parse(Config *config, int argc, const char *argv[]) {
  flag_str(&config->config_file, "config", "Path to config file");
  flag_str(&config->layout_file, "layout", "Path to layout file");
  flag_parse(argc, argv, VERSION);

  if (access(config->config_file, F_OK) == 0 &&
      ini_parse(config->config_file, handler, config) < 0) {
    fprintf(stderr, "failed to load '%s'\n", config->config_file);
    exit(1);
  }
}

void config_unique_merge_mpv_flags(ConfigMpvFlags *to, ConfigMpvFlags from) {
  for (int from_i = 0; from_i < from.count; from_i++) {
    if (to->count == MAX_MPV_FLAGS)
      return;

    for (int to_i = 0; to_i < to->count; to_i++) {
      if (strcmp(to->flags[to_i].name, from.flags[from_i].name) == 0) {
        goto end; // break outer loop
      }
    }

    to->flags[to->count].name = from.flags[from_i].name;
    to->flags[to->count].data = from.flags[from_i].data;
    to->count++;

  end: {}
  }
}
