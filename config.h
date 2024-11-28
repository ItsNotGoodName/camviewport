#pragma once

#include "main.h"
#include <X11/X.h>

typedef struct {
  char *name;
  char *data;
} ConfigMpvFlag;

typedef struct {
  int count;
  ConfigMpvFlag flags[MAX_MPV_FLAGS];
} ConfigMpvFlags;

typedef struct {
  char *name;
  char *main;
  char *sub;
  ConfigMpvFlags mpv_flags;
} ConfigStream;

typedef struct {
  KeySym quit;
  KeySym home;
  KeySym next;
  KeySym previous;
  KeySym reload;
} ConfigKeyMap;

typedef struct {
  int show_version;
  char *config_file;
  char *layout_file;
  ConfigMpvFlags mpv_flags;
  int stream_count;
  ConfigStream streams[MAX_STREAMS];
  ConfigKeyMap key_map;
} Config;

void config_parse(Config *config, int argc, char *argv[]);
