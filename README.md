# CamViewport

Simple video wall for viewing IP cameras.

X11 and mpv is used to display multiple low latency RTSP streams.
Each stream is a X11 window with a mpv player embedded through the `--wid` mpv option.

There are three views, fullscreen, grid, and layout.
The layout view requires passing a layout file which allows manual placement of streams.

## Installation

The preferred method is to use the [Ansible Role](https://github.com/ItsNotGoodName/ansible-role-camviewport) which sets up a headless installation.

## Requirements

```
sudo apt install xserver-xorg-core xinit libmpv2
```

## Configuration

Configuration file is at `camviewport.ini`.

### Global Variables

| Variables    | Description                                                                                                        | Example |
| ------------ | ------------------------------------------------------------------------------------------------------------------ | ------- |
| `layout`     | Layout file path                                                                                                   |
| `key-*`      | Key binding where `*` is a X11 key without `XK_` prefix, see [Key Actions](#key-actions) for values                |         |
| `mpv-*`      | mpv option where `*` is the [mpv option](https://mpv.io/manual/master/#options)                                    |         |
| `main-mpv-*` | mpv property where `*` is the [mpv property](https://mpv.io/manual/master/#properties) when main stream is playing |         |
| `sub-mpv-*`  | mpv property where `*` is the [mpv property](https://mpv.io/manual/master/#properties) when sub stream is playing  |         |

### Stream Variables

| Variables    | Description                                                                    | Example |
| ------------ | ------------------------------------------------------------------------------ | ------- |
| `main`       | RTSP stream only used when the view is fullscreen or grid with a single stream |         |
| `sub`        | RTSP stream                                                                    |         |
| `mpv-*`      | See [Global Variables](#global-variables)                                      |         |
| `main-mpv-*` | See [Global Variables](#global-variables)                                      |         |
| `sub-mpv-*`  | See [Global Variables](#global-variables)                                      |         |

### Key Actions

| Key     | Action     | Description            |
| ------- | ---------- | ---------------------- |
| `q`     | `quit`     | Close the program      |
| `r`     | `reload`   | Reload the layout file |
| `space` | `home`     | Toggle fullscreen      |
| `l`     | `next`     | Go to next pane        |
| `h`     | `previous` | Go to previous pane    |

### Example

```ini
; Key map
key-q = quit

; Global mpv option
mpv-hwdec = vaapi

[CAM-01]
; Main stream
main = rtsp://admin:password@192.168.1.108:554/cam/realmonitor?channel=1&subtype=0
; Sub stream
sub = rtsp://admin:password@192.168.1.108:554/cam/realmonitor?channel=1&subtype=1

; Stream mpv option
mpv-video-rotate = 90
; Main stream mpv property
main-mpv-keepaspect = yes
; Sub stream mpv property
sub-mpv-keepaspect = no
```

## Development

```
sudo apt install build-essential libmpv-dev
```
