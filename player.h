#pragma once

#include <mpv/client.h>

void player_loadfile(mpv_handle *mpv, char *stream);
void player_stop(mpv_handle *mpv);
void player_set_speed(mpv_handle *mpv, double speed);
