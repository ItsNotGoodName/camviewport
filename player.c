#include <mpv/client.h>
#include <stdio.h>

void player_loadfile(mpv_handle *mpv, char *stream) {
  const char *cmd[] = {"loadfile", stream, NULL};
  int err = mpv_command(mpv, cmd) < 0;
  if (err < 0)
    fprintf(stderr, "player: failed to play file: %d\n", err);
}

void player_stop(mpv_handle *mpv) {
  const char *cmd[] = {"stop", NULL};
  int err = mpv_command(mpv, cmd) < 0;
  if (err < 0)
    fprintf(stderr, "player: failed to stop file: %d\n", err);
}

void player_set_speed(mpv_handle *mpv, double speed) {
  int err = mpv_set_property(mpv, "speed", MPV_FORMAT_DOUBLE, &speed);
  if (err < 0)
    fprintf(stderr, "player: failed to set speed: %d\n", err);
}
