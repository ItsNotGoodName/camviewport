#include "layout.h"
#include "inih/ini.h"
#include "util.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

LayoutGrid layout_grid_new(int width, int height, int count) {
  int columns = 0;
  int rows = 0;
  while (columns * rows < count) {
    columns++;
    if (columns * rows >= count)
      break;
    rows++;
  }

  int pane_width = width * (1.0 / columns);
  int pane_height = height * (1.0 / rows);

  LayoutGrid g = {
      .width = width,
      .height = height,
      .pane_width = pane_width,
      .pane_height = pane_height,
      .columns = columns,
  };
  return g;
}

LayoutWindow layout_grid_window(LayoutGrid layout_grid, int index) {
  int row = floor((double)index / layout_grid.columns);
  int col = index % layout_grid.columns;

  LayoutWindow layout_pane = {
      .x = layout_grid.pane_width * col,
      .y = layout_grid.pane_height * row,
      .width = layout_grid.pane_width,
      .height = layout_grid.pane_height,
  };
  return layout_pane;
}

LayoutWindow layout_pane_window(LayoutPane pane, int width, int height) {
  LayoutWindow w = {
      .x = (int)(pane.x * width),
      .y = (int)(pane.y * height),
      .width = (int)(pane.width * width),
      .height = (int)(pane.height * height),
  };
  return w;
}

static double ratio(const char *maybe_ratio) {
  char *slash = strchr(maybe_ratio, '/');
  if (!slash)
    return atof(maybe_ratio);

  char prev = *slash;
  *slash = 0;
  double top = atof(maybe_ratio);
  *slash = prev;

  double bot = atof(slash + sizeof(char));

  return top / bot;
}

static int handler(void *user, const char *section, const char *name,
                   const char *value) {
  LayoutFile *file = user;

#define MATCH(n) strcmp(name, n) == 0

  if (strcmp(section, "") == 0) {
    // Global
    if (MATCH("name")) {
      file->name = strdup(value);
    } else {
      return 0;
    }
  } else {
    int pane = atoi(section);
    if (pane > MAX_STREAMS)
      die("too many panes");
    if (pane < 1) {
      fprintf(stderr, "invalid pane: %d\n", pane);
      exit(1);
    }

    if (MATCH("x")) {
      file->panes[pane - 1].x = ratio(value);
    } else if (MATCH("y")) {
      file->panes[pane - 1].y = ratio(value);
    } else if (MATCH("w")) {
      file->panes[pane - 1].width = ratio(value);
    } else if (MATCH("h")) {
      file->panes[pane - 1].height = ratio(value);
    } else {
      return 0;
    }

    if (file->pane_count < pane)
      file->pane_count = pane;
  }

  return 1;
}

int layout_reload(LayoutFile *layout_file, const char *file_path) {
  if (layout_file->name)
    free(layout_file->name);
  return ini_parse(file_path, handler, layout_file);
}
