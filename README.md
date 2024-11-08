# CamViewport

# Configuration

Default location is `./camviewport.ini`.

```ini
; MPV's hwdec option.
hwdec = vaapi
view = grid,1+5

[CAM-01]
; Main stream used in fullscreen and single view.
main = rtsp://admin:password@192.168.1.108:554/cam/realmonitor?channel=1&subtype=0
; Sub stream used in multi view. (optional)
sub = rtsp://admin:password@192.168.1.108:554/cam/realmonitor?channel=1&subtype=1
```

# Development

```
sudo apt install build-essential libmpv-dev
```
