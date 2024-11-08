#pragma once

typedef struct {
  int x;
  int y;
  int width;
  int height;
} LayoutWindow;

typedef struct {
  double x;
  double y;
  double width;
  double height;
} LayoutPane;

typedef struct {
  int width;
  int height;
  int pane_width;
  int pane_height;
  int columns;
} LayoutGrid;

LayoutGrid layout_grid_new(int width, int height, int count);

LayoutWindow layout_grid_window(LayoutGrid layout_grid, int index);

LayoutWindow layout_pane_window(LayoutPane pane, int width, int height);
