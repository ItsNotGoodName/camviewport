#include <stdint.h>
#include <sys/time.h>
#include <time.h>

static int64_t time_now() {
  struct timeval tp;
  gettimeofday(&tp, NULL);
  return tp.tv_sec * 1000 + tp.tv_usec / 1000;
}

static int64_t interval_ms = 16; // 60fps
static int64_t start_ms;

void clock_set_fps(int fps) { interval_ms = (1.0 / fps) * 1000; }

void clock_start() { start_ms = time_now(); }

void clock_wait() {
  int64_t now_ms = time_now();
  int64_t sleep_ms = interval_ms - (now_ms - start_ms);
  if (sleep_ms < 0)
    return;

  struct timespec ts;
  ts.tv_sec = sleep_ms / 1000;
  ts.tv_nsec = (sleep_ms % 1000) * 1000000;
  nanosleep(&ts, NULL);
}
