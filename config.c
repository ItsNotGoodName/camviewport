#include "config.h"
#include "util.h"
#include <X11/X.h>
#include <X11/Xlib.h>
#include <string.h>

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

int config_file_parser(void *user, const char *section, const char *name,
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
        config->key_map.quit = key_sym;
      else if (VALUE("home"))
        config->key_map.home = key_sym;
      else if (VALUE("next"))
        config->key_map.next = key_sym;
      else if (VALUE("previous"))
        config->key_map.previous = key_sym;
      else
        return 0;
    } else
      return 0;
    return 1;
  }

  // Get index
  int index = -1;
  for (int i = 0; i < config->stream_count; i++) {
    if (strcmp(config->streams[i].name, section) == 0) {
      // Found
      index = i;
      break;
    }
  }
  if (index == -1) {
    // Create
    if (config->stream_count == MAX_STREAMS)
      die("too many streams");
    index = config->stream_count;

    config->streams[index].name = strdup(section);
    config->stream_count++;
  }

  if (MATCH("main")) {
    config->streams[index].main = strdup(value);
  } else if (MATCH("sub")) {
    config->streams[index].sub = strdup(value);
  } else if (MATCH_MPV) {
    parse_mpv_flag(&config->streams[index].mpv_flags, name, value);
  } else {
    return 0;
  }

  return 1;
}
