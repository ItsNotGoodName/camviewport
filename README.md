# CamViewport

# Requirements

```
sudo apt install xserver-xorg-core xinit libmpv2
```

# Configuration

File `camviewport.ini`.

```ini
; mpv options are prefixed with mpv-
mpv-hwdec = vaapi

; Key maps
key-q = quit
key-Home = home
key-Right = next
key-Left = previous

[CAM-01]
; Main stream used in fullscreen and single view.
main = rtsp://admin:password@192.168.1.108:554/cam/realmonitor?channel=1&subtype=0
; Sub stream used in multi view. (optional)
sub = rtsp://admin:password@192.168.1.108:554/cam/realmonitor?channel=1&subtype=1
mpv-video-rotate = 90
```

# Development

```
sudo apt install build-essential libmpv-dev
```
