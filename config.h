#pragma once

#include "main.h"

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
  ConfigMpvFlags flags;
} ConfigStream;

typedef struct {
  char *config_file;
  char *layout_file;
  ConfigMpvFlags flags;
  int stream_count;
  ConfigStream streams[MAX_STREAMS];
} Config;

int config_file_parser(void *user, const char *section, const char *name,
                       const char *value);
