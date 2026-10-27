wcap
====

Simple and efficient screen recording utility for Windows.

Get latest binary here: [wcap-x64.exe][] or [wcap-arm64.exe][]

**WARNING**: Windows Defender or other AV software might report false positive detection

中文说明（本分支改动）
======================

界面已全部汉化（托盘菜单、设置对话框、录制区域提示、通知气泡）。在原有功能上新增：

 * **全局快捷键（可在设置里修改）**
   * `Ctrl + Alt + R` 开始录制（默认录制鼠标所在显示器），只负责开始，不会再停止
   * `Ctrl + Alt + S` 停止录制
   * `Ctrl + PrintScreen` 录制显示器、`Ctrl + Win + PrintScreen` 录制窗口、`Ctrl + Shift + PrintScreen` 录制区域（原有，录制中再按会停止）
   * **独占全屏游戏里也能用**：除 `RegisterHotKey` 外还挂了一个低级键盘钩子（`WH_KEYBOARD_LL`）。像三国志12这类用 `ChangeDisplaySettingsEx` 切独占全屏的老游戏，系统不会投递 `WM_HOTKEY`，钩子则能在原始输入线程之前看到按键。默认开启，设置里“快捷键 → 全屏热键兼容”可关闭
 * **自定义文件名**：设置里“文件名格式”支持占位符，默认 `wcap-{date}-{seq}`，输出如 `wcap-2026-10-19-01.mp4`
   * `{date}` → 2026-10-19，`{time}` → 14-32-05，`{datetime}` → 2026-10-19_14-32-05
   * `{yyyy}` `{yy}` `{MM}` `{dd}` `{HH}` `{mm}` `{ss}` 单独的年月日时分秒
   * `{seq}`（或 `{index}`、`{n}`）→ 两位递增序号，自动跳过当天已存在的文件；模板里不写 `{seq}` 时，若同名文件已存在会自动追加 `-01`
   * 文件名中的非法字符会自动替换为 `-`
 * **开始/停止提示音**：开始录制响一声（880 Hz），停止录制连响两声（660 Hz + 间隔 + 660 Hz），异步播放、不阻塞录制；可在设置里关闭“录制提示音”
 * **录制状态指示器**：屏幕右上角的小圆点，录制中绿色、未录制红色，始终置顶，全屏游戏（无边框全屏）下也可见；窗口使用 `WDA_EXCLUDEFROMCAPTURE`，不会被录进视频，也不会造成捕获回环。可在设置里关闭“显示状态指示器”
 * **托盘右键菜单**：`访问主页`（https://github.com/xinzi1229/wcap）、`打开录制目录`（不存在时自动创建）、`设置`（录制中禁用）、`退出`

Features
========

 * press <kbd>Ctrl + PrintScreen</kbd> to start recording current monitor (where mouse cursor currently is positioned)
 * press <kbd>Ctrl + Win + PrintScreen</kbd> to start recording currently active window
 * press <kbd>Ctrl + Shift + PrintScreen</kbd> to select & record fixed region on current monitor
 * press any of previous combinations to stop recording
 * right or double-click on tray icon to change settings
 * video encoded using [H264/AVC][], [H265/HEVC][] or [AV1][], with 10-bit support for HEVC and AV1
 * audio encoded using [AAC][] or [FLAC][]
 * for window capture record full window area (including title bar/borders) or just the client area
 * window capture can record **application local audio**, no other system/process audio included
 * options to exclude mouse cursor from capture, disable recording indication borders, or rounded window corners
 * can limit recording length in seconds or file size in MB's
 * can limit max width, height or framerate - captured frames will be automatically downscaled
 * when limiting max width/height - can perform **gamma correct resize**
 * optional **improved color conversion** - adjust output YUV values to better match brightness to original RGB input

Details
=======

wcap uses [Windows.Graphics.Capture][wgc] API available since **Windows 10 version 1903, May 2019 Update (19H1)** to capture
contents of window or whole monitor. Captured texture is submitted to Media Foundation to encode video to mp4 file with
hardware accelerated codec. Using capture from compositor and hardware accelerated encoder allows it to consume very
little CPU and memory.

You can choose in settings to capture only client area or full size of window - client area will not include title bar and
borders for standard windows style. Recorded video size is determined by initial window size.

By default hardware encoder is enabled, you can disable it in settings Make sure your GPU drivers are updated if something is
not working with hardware video encoding. Then video will be encoded using [Microsoft Media Foundation H264][MSMFH264]
software encoder. You might want to explicitly use software encoder on older GPU's as their hardware encoder quality is not great.

Audio is captured using [WASAPI loopback recording][] and encoded using [Microsoft Media Foundation AAC][MSMFAAC] encoder, or
undocumented Media Foundation FLAC encoder (it seems it always is present in Windows 10 and 11).

Recorded mp4 file can be set to use fragmented mp4 format in settings (only for H264 codec). Fragmented mp4 file does not
require "finalizing" it. Which means that in case application or GPU driver crashes or if you run out of disk space then
the partial mp4 file will be valid for playback. The disadvantage of fragmented mp4 file is that it is a bit larger than
normal mp4 format, and seeking is slower.

You can use settings dialog to restrict max resolution of video - captured image will be scaled down to keep aspect ratio
if you set any of max width/height settings to non-zero value. Similarly framerate of capture can be reduced to limit
maximum amount of frames per second. Setting it to zero will use compositor framerate which is typically monitor refresh
rate. Lower video framerate will give higher quality video for same bitrate and reduced GPU usage. If you notice too many
dropped frames during recording, try reducing video resolution and framerate.

Capture of mouse cursor can be disabled only when using Windows 10 version 2004, May 2020 Update (20H1) or newer.

On Windows 11 you can disable yellow recording borders, or rounded window corners. On Windows 11 24H2 version can enable secondary
window capture, when they intersect main window in window-only capture mode - for popups & tool windows like "Open File" dialog.

HEVC Software Encoding
======================

HEVC encoding in software (on CPU) will require installing HEVC Video Extensions from Windows Store. It will support only
8-bit encoding. You can get direct download to installer package without using Windows Store application with following steps:

1) open https://store.rg-adguard.net/
2) search `https://www.microsoft.com/store/productId/9n4wgh0z6vhq` for `Retail` channel
3) download & run .appxbundle package it gives you

Creating gif from mp4
=====================

If you want to create gif file out of recorded mp4 file, you can use following .bat file:

    ffmpeg.exe -hide_banner -nostdin -loglevel fatal -stats -y -i %1 -filter_complex "[0]fps=15,split[v0][v1];[v0]palettegen=stats_mode=full[p];[v1][p]paletteuse" %~n1.gif

Or to create new palette every frame for more colors, but larger file size:

    ffmpeg.exe -hide_banner -nostdin -loglevel fatal -stats -y -i %1 -filter_complex "[0]fps=15,split[v0][v1];[v0]palettegen=stats_mode=single[p];[v1][p]paletteuse=new=1" %~n1.gif

Put this line in `make_gif.bat` file, place [ffmpeg][] executable next to it and then simply drag & drop .mp4 file on top of it.
Change `fps=15` to desired gif fps (or remove to use original video fps). Check the [paletteuse][] filter arguments for
different dither methods.

Building
========

To build the binary from source code, have [Visual Studio][VS] installed, and simply run `build.cmd`.

License
=======

This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or distribute this software, either in source code form or as
a compiled binary, for any purpose, commercial or non-commercial, and by any means.

[wcap-x64.exe]: https://raw.githubusercontent.com/wiki/mmozeiko/wcap/wcap-x64.exe
[wcap-arm64.exe]: https://raw.githubusercontent.com/wiki/mmozeiko/wcap/wcap-arm64.exe
[wgc]: https://blogs.windows.com/windowsdeveloper/2019/09/16/new-ways-to-do-screen-capture/
[MSMFH264]: https://docs.microsoft.com/en-us/windows/win32/medfound/h-264-video-encoder
[VS]: https://visualstudio.microsoft.com/vs/
[WASAPI loopback recording]: https://docs.microsoft.com/en-us/windows/win32/coreaudio/loopback-recording
[MSMFAAC]: https://docs.microsoft.com/en-us/windows/win32/medfound/aac-encoder
[ffmpeg]: https://ffmpeg.org/
[paletteuse]: https://ffmpeg.org/ffmpeg-filters.html#paletteuse
[H264/AVC]: https://en.wikipedia.org/wiki/Advanced_Video_Coding
[H265/HEVC]: https://en.wikipedia.org/wiki/High_Efficiency_Video_Coding
[AV1]: https://en.wikipedia.org/wiki/AV1
[AAC]: https://en.wikipedia.org/wiki/Advanced_Audio_Coding
[FLAC]: https://en.wikipedia.org/wiki/FLAC
