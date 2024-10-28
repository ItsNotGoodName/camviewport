#pragma once
#include <stdint.h>

struct LayoutPane {
  int x;
  int y;
  int width;
  int height;
};

struct LayoutGrid {
  int width;
  int height;
  int pane_width;
  int pane_height;
  int columns;
};

struct LayoutGrid layout_grid(int width, int height, int count);

struct LayoutPane layout_grid_pane(struct LayoutGrid, int index);
