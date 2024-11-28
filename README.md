# CamViewport

Simple video wall for viewing IP cameras.

CamViewport uses X11 and mpv to display a low latency RTSP stream.
Each stream has a X11 window with a mpv player embedded through the `--wid` option.

There are three views, fullscreen, grid, and layout.
The layout view requires passing a layout file which will allow manual stream placement.

Options can be passed to mpv by using `mpv-*` name where `*` is the [mpv option](https://mpv.io/manual/master/#options).

Streams can contain a main stream and a sub stream.
Sub stream is only used when the view is not fullscreen or grid with a single stream.

Key maps can be defined via `key-*` name where `*` is a X11 key without `XK_`.

# Requirements

```
sudo apt install xserver-xorg-core xinit libmpv2
```

# Configuration

File `camviewport.ini`.

```ini
mpv-hwdec = vaapi

; Key maps
key-q = quit
key-Home = home
key-Right = next
key-Left = previous

[CAM-01]
; Main stream
main = rtsp://admin:password@192.168.1.108:554/cam/realmonitor?channel=1&subtype=0
; Sub stream
sub = rtsp://admin:password@192.168.1.108:554/cam/realmonitor?channel=1&subtype=1
mpv-video-rotate = 90
```

# Development

```
sudo apt install build-essential libmpv-dev
```
