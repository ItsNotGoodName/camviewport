#include "layout.h"
#include <math.h>

struct LayoutGrid layout_grid_new(int width, int height, int count) {
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

  struct LayoutGrid g = {
      .width = width,
      .height = height,
      .pane_width = pane_width,
      .pane_height = pane_height,
      .columns = columns,
  };
  return g;
}

struct LayoutPane layout_grid_pane(struct LayoutGrid layout_grid, int index) {
  int row = floor((double)index / layout_grid.columns);
  int col = index % layout_grid.columns;

  struct LayoutPane layout_pane = {
      .x = layout_grid.pane_width * col,
      .y = layout_grid.pane_height * row,
      .width = layout_grid.pane_width,
      .height = layout_grid.pane_height,
  };
  return layout_pane;
}
