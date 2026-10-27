Changelog
=========

##### 2026.10.27
 * 托盘右键菜单首项改为“访问主页”，指向 https://github.com/xinzi1229/wcap
 * 托盘右键菜单新增“打开录制目录”，直接打开设置里的输出文件夹（不存在时自动创建）

##### 2026.10.22
 * 修复快捷键“录一下就停”：开始快捷键只负责开始，不再把刚启动的录制停掉（根因是同一次按键经 RegisterHotKey 和键盘钩子各投递一次，启动录制阻塞主线程超过 250ms 去重窗口，第二次投递被当成新的按键）
 * “开始/停止录制”快捷键改名为“开始录制”，“仅停止录制”改名为“停止录制”
 * 托盘菜单的 wcap 链接改为 https://github.com/xinzi1229/wcap

##### 2026.10.20
 * 新增低级键盘钩子作为快捷键的第二条通路：独占全屏游戏（如三国志12）下 RegisterHotKey 收不到 WM_HOTKEY，快捷键现在仍可用
 * 快捷键组新增“全屏热键兼容”开关（默认开启，INI: HotKeyHook），关掉后只走 RegisterHotKey
 * 钩子运行在独立线程，回调不阻塞主线程；同一组合键只会触发一次动作

##### 2026.10.19
 * 增加“开始/停止录制”“仅停止录制”两个全局快捷键（默认 Ctrl+Alt+R / Ctrl+Alt+S）
 * 增加自定义输出文件名模板，支持 {date} {time} {datetime} {seq} 等占位符，默认 wcap-{date}-{seq}
 * 增加屏幕右上角录制状态指示器：录制中为绿色，未录制为红色，全屏游戏下仍置顶显示（不进入录像画面）
 * 开始录制提示音响一声，结束录制提示音响两声，可在设置中关闭
 * 界面汉化：托盘菜单、设置对话框、区域选择提示、通知气泡
 * build.cmd 增加 /utf-8 编译选项，并在 VS 工作负载探测失败时回退到最新安装

##### 2025.11.23
 * option to capture secondary windows (popups & dialogs) on Windows 11 24H2 and up

##### 2025.05.25
 * fix screen capture framerate on Windows 11 24H2

##### 2025.02.10
 * fix border still showing when cannot start region capture
 * fix typo when saving config

##### 2024.10.18
 * add native arm64 Windows support
 * add AV1 codec for encoding - 8-bit and 10-bit main profile

##### 2024.09.15
 * record application local audio for window capture (Windows 10 "20H1" and up)
 * allow to disable yellow capture border indicator (Windows 11)
 * allow to disable rounded window corners for window capture (Windows 11)
 * resize performance improvements when limiting max width/height
 * option to do gamma correct resize
 * option to improve RGB to YUV color conversion
 * hardcode keyboard shorcut names, GetKeyNameText does not work for all keys
 * raised default size limit from 8MB to 25MB
 * renamed "capture rectangle" to "capture region" in config UI
 * bunch of code cleanup, now doing single-TU build

##### 2023.09.21
 * predefined region capture sizes

##### 2023.09.16
 * check that WASAPI device timestamps are usable, otherwise fall back to stream position

##### 2022.11.06
 * remove notifications on start & stop of capture
 * remove Windows 10 version check on startup, code always assumes Windows 10
 * support for 10-bit HEVC encoding

##### 2021.12.21
 * allow to choose integrated vs discrete GPU to use for encoding
 * keep proper encoded video stream time when no new frames are captured

##### 2021.12.18
 * fixed wrong audio timestamps when encoding audio
 * fixed wrong d3d11 calls to copy texture when size is odd
 * fixed hanging when encoding audio & video with too many dropped frames

##### 2021.12.08
 * fixed compute shaders to work on older D3D11 hardware

##### 2021.12.05
 * allow to selected limited vs full range for YUV conversion

##### 2021.12.04
 * improved resizing and YUV conversion quality
 * improved performance for drawing background for rectangle selection
 * fix crash when child window is in foreground for window capture

##### 2021.10.17
 * allow to configure keyboad shortcuts

##### 2021.10.04
 * option to encode video with HEVC codec
 * option to encode audio with FLAC codec
 * allow limit file length or size
 * allow to choose output folder location
 * customize audio codec channels & samplerate

##### 2021.09.25
 * allow to capture fixed position rectangle area on screen
 * prevent config dialog to be open multiple times

##### 2021.09.20
 * added WASAPI loopback recording
 * audio is encoded using AAC codec
 * fix crash when capturing toolbar window

##### 2021.09.19
 * initial release
