#include "layout.h"
#include "util.h"
#include <math.h>
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

static double ratio(const char *ratio) {
  // if num , err : = strconv.ParseFloat(ratio, 32);
  // err == nil{return float32(num), err}
  //
  //     f : = strings.Split(ratio, "/") fLen : = len(f) if fLen == 2 {
  //   num,
  //       err : = strconv.ParseFloat(f[0], 32) if err != nil{return 0, err}
  //
  //               den,
  //           err : = strconv.ParseFloat(f[1], 32) if err != nil {
  //     return 0, err
  //   }
  //
  //   return float32(num) / float32(den), nil
  // }
  //
  // return 0, fmt.Errorf("%s: invalid float", ratio)
}

int layout_file_parser(void *user, const char *section, const char *name,
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
    int index = atoi(section);
    if (index > MAX_STREAMS)
      die("too many panes");

    if (MATCH("x")) {
      file->panes[index].x = atof(value);
    } else if (MATCH("y")) {
      file->panes[index].y = atof(value);
    } else if (MATCH("w")) {
      file->panes[index].width = atof(value);
    } else if (MATCH("h")) {
      file->panes[index].height = atof(value);
    } else {
      return 0;
    }
  }

  return 1;
}
