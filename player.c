#include <mpv/client.h>
#include <stdio.h>

void player_loadfile(mpv_handle *mpv, char *stream) {
  const char *cmd2[] = {"loadfile", stream, NULL};
  int err2 = mpv_command(mpv, cmd2) < 0;
  if (err2 < 0)
    fprintf(stderr, "failed to play file: error %d\n", err2);
}

void player_stop(mpv_handle *mpv) {
  const char *cmd[] = {"stop", NULL};
  int err = mpv_command(mpv, cmd) < 0;
  if (err < 0)
    fprintf(stderr, "failed to stop file: error %d\n", err);
}
