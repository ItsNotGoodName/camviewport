# CamViewport

Simple video wall for viewing IP cameras.

X11 and mpv is used to display multiple low latency RTSP streams.
Each stream is a X11 window with a mpv player embedded through the `--wid` mpv option.

There are three views, fullscreen, grid, and layout.
The layout view requires passing a layout file which allows manual placement of streams.

Streams can contain a main stream and a sub stream.
Main stream is only used when the view is fullscreen or grid with a single stream.

Options can be passed in the configuration by using `mpv-*` where `*` is the [mpv option](https://mpv.io/manual/master/#options).

Key maps are defined in the configuration with `key-*` where `*` is a X11 key without `XK_` prefix.

# Installation

The preferred method is to use the [Ansible Role](https://github.com/ItsNotGoodName/ansible-role-camviewport) which sets up a headless installation.

# Requirements

```
sudo apt install xserver-xorg-core xinit libmpv2
```

# Configuration

File `camviewport.ini`.

```ini
; Global mpv option
mpv-hwdec = vaapi

; Key maps
key-q = quit
key-space = home
key-l = next
key-h = previous

[CAM-01]
; Main stream
main = rtsp://admin:password@192.168.1.108:554/cam/realmonitor?channel=1&subtype=0
; Sub stream
sub = rtsp://admin:password@192.168.1.108:554/cam/realmonitor?channel=1&subtype=1
; Scoped mpv option
mpv-video-rotate = 90
```

# Development

```
sudo apt install build-essential libmpv-dev
```
