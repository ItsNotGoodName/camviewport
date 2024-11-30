#include <mpv/client.h>
#include <stdio.h>

void player_loadfile(mpv_handle *mpv, char *stream) {
  // printf("I AM PLAYING\n");
  const char *cmd[] = {"set", "pause", "no", NULL};
  int err = mpv_command(mpv, cmd) < 0;
  if (err < 0)
    fprintf(stderr, "failed to unpause file: error %d\n", err);
  const char *cmd2[] = {"loadfile", stream, NULL};
  int err2 = mpv_command(mpv, cmd2) < 0;
  if (err2 < 0)
    fprintf(stderr, "failed to play file: error %d\n", err2);
}

void player_pause(mpv_handle *mpv) {
  // printf("I AM PAUSING\n");
  const char *cmd[] = {"set", "pause", "yes", NULL};
  int err = mpv_command(mpv, cmd) < 0;
  if (err < 0)
    fprintf(stderr, "failed to pause file: error %d\n", err);
}
