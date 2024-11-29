#include <mpv/client.h>
#include <stdio.h>

void player_loadfile(mpv_handle *mpv, char *stream) {
  const char *cmd[] = {"loadfile", stream, NULL};
  int err = mpv_command(mpv, cmd) < 0;
  if (err < 0)
    fprintf(stderr, "failed to play file: error %d", err);
}

void player_pause(mpv_handle *mpv) {
  const char *cmd[] = {"set", "property", "pause", "true", NULL};
  int err = mpv_command(mpv, cmd) < 0;
  if (err < 0)
    fprintf(stderr, "failed to stop file: error %d", err);
}
