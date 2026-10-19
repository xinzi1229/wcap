#include "wcap.h"
#include "wcap_config.h"
#include "wcap_audio_capture.h"
#include "wcap_screen_capture.h"
#include "wcap_encoder.h"

#include <dxgi1_6.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <windowsx.h>
#include <mmsystem.h>
#include <math.h>
#include <wchar.h>

#pragma comment (lib, "ntdll")
#pragma comment (lib, "kernel32")
#pragma comment (lib, "user32")
#pragma comment (lib, "gdi32")
#pragma comment (lib, "msimg32")
#pragma comment (lib, "dxgi")
#pragma comment (lib, "d3dcompiler")
#pragma comment (lib, "d3d11")
#pragma comment (lib, "dwmapi")
#pragma comment (lib, "shell32")
#pragma comment (lib, "shlwapi")
#pragma comment (lib, "mfplat")
#pragma comment (lib, "mfuuid")
#pragma comment (lib, "mfreadwrite")
#pragma comment (lib, "evr")
#pragma comment (lib, "strmiids")
#pragma comment (lib, "ksuser")
#pragma comment (lib, "mmdevapi")
#pragma comment (lib, "ole32")
#pragma comment (lib, "wmcodecdspuuid")
#pragma comment (lib, "avrt")
#pragma comment (lib, "uxtheme")
#pragma comment (lib, "winmm")
#pragma comment (lib, "OneCore")
#pragma comment (lib, "CoreMessaging")

#if defined(_M_AMD64)
// this is needed to be able to use Nvidia Media Foundation encoders on Optimus systems
__declspec(dllexport) DWORD NvOptimusEnablement = 1;
#endif

#define WM_WCAP_ALREADY_RUNNING (WM_USER+1)
#define WM_WCAP_STOP_CAPTURE    (WM_USER+2)
#define WM_WCAP_TRAY_TITLE      (WM_USER+3)
#define WM_WCAP_COMMAND         (WM_USER+4)

#define WCAP_AUDIO_CAPTURE_TIMER    1
#define WCAP_AUDIO_CAPTURE_INTERVAL 100 // msec

#define WCAP_VIDEO_UPDATE_TIMER     2
#define WCAP_VIDEO_UPDATE_INTERVAL  100 // msec

#define CMD_WCAP     1
#define CMD_QUIT     2
#define CMD_SETTINGS 3

#define HOT_RECORD_WINDOW  1
#define HOT_RECORD_MONITOR 2
#define HOT_RECORD_REGION  3
#define HOT_RECORD_TOGGLE  4
#define HOT_RECORD_STOP    5
#define HOT_COUNT          5

#define WCAP_RESIZE_NONE 0
#define WCAP_RESIZE_TL   1
#define WCAP_RESIZE_T    2
#define WCAP_RESIZE_TR   3
#define WCAP_RESIZE_L    4
#define WCAP_RESIZE_M    5
#define WCAP_RESIZE_R    6
#define WCAP_RESIZE_BL   7
#define WCAP_RESIZE_B    8
#define WCAP_RESIZE_BR   9

#define WCAP_UI_FONT      L"Microsoft YaHei UI"
#define WCAP_UI_FONT_SIZE 16

#define WCAP_RECT_BORDER 2

// file name template support: {date}, {time}, {datetime}, {yyyy}, {yy}, {MM}, {dd}, {HH}, {mm}, {ss}, {seq}
#define WCAP_SEQ_MARKER                ((WCHAR)1)
#define WCAP_FILENAME_MAX              192
#define WCAP_FILENAME_TEMPLATE_MAX     128

// notification sounds (generated at startup, played asynchronously)
#define WCAP_SOUND_RATE     22050
#define WCAP_SOUND_MAX_SIZE 16384

typedef struct
{
	WORD Freq; // Hz
	WORD Msec; // duration in milliseconds
}
SoundTone;

static const SoundTone gSoundTonesStart[] = { { 880, 70 }, { 1320, 90 } };
static const SoundTone gSoundTonesStop[]  = { { 1320, 70 }, { 660, 110 } };

static BYTE gSoundStart[WCAP_SOUND_MAX_SIZE];
static BYTE gSoundStop[WCAP_SOUND_MAX_SIZE];

// constants
static WCHAR gConfigPath[MAX_PATH];
static LARGE_INTEGER gTickFreq;
static HICON gIcon1;
static HICON gIcon2;
static UINT WM_TASKBARCREATED;
static HCURSOR gCursorArrow;
static HCURSOR gCursorClick;
static HCURSOR gCursorResize[10];
static HFONT gFont;
static HFONT gFontBold;

// recording state
static BOOL gRecordingStarted;
static BOOL gRecording;
static DWORD gRecordingLimitFramerate;
static DWORD gRecordingDroppedFrames;
static UINT64 gRecordingLastFrame;
static UINT64 gRecordingNextEncode;
static UINT64 gRecordingNextTooltip;
static EXECUTION_STATE gRecordingState;
static WCHAR gRecordingPath[MAX_PATH];

// when selecting rectangle to record
static HMONITOR gRectMonitor;
static HDC gRectContext;
static HDC gRectDarkContext;
static HBITMAP gRectBitmap;
static HBITMAP gRectDarkBitmap;
static DWORD gRectWidth;
static DWORD gRectHeight;
static BOOL gRectSelected;
static POINT gRectSelection[2];
static POINT gRectMousePos;
static int gRectResize;
static int gRectSetSize[2];
static BOOL gRectSetSizeClick;

// globals
static HWND gWindow;
static Config gConfig;
static AudioCapture gAudio;
static ScreenCapture gCapture;
static Encoder gEncoder;

static void ShowNotification(LPCWSTR Message, LPCWSTR Title, DWORD Flags)
{
	NOTIFYICONDATAW Data =
	{
		.cbSize = sizeof(Data),
		.hWnd = gWindow,
		.uFlags = NIF_INFO | NIF_TIP,
		.dwInfoFlags = Flags, // NIIF_INFO, NIIF_WARNING, NIIF_ERROR
	};
	StrCpyNW(Data.szTip, WCAP_TITLE, _countof(Data.szTip));
	StrCpyNW(Data.szInfo, Message, _countof(Data.szInfo));
	StrCpyNW(Data.szInfoTitle, Title ? Title : WCAP_TITLE, _countof(Data.szInfoTitle));
	Shell_NotifyIconW(NIM_MODIFY, &Data);
}

static void UpdateTrayTitle(LPCWSTR Title)
{
	NOTIFYICONDATAW Data =
	{
		.cbSize = sizeof(Data),
		.hWnd = gWindow,
		.uFlags = NIF_TIP,
	};
	StrCpyNW(Data.szTip, Title, _countof(Data.szTip));
	Shell_NotifyIconW(NIM_MODIFY, &Data);
}

static void UpdateTrayIcon(HICON Icon)
{
	NOTIFYICONDATAW Data =
	{
		.cbSize = sizeof(Data),
		.hWnd = gWindow,
		.uFlags = NIF_ICON,
		.hIcon = Icon,
	};
	Shell_NotifyIconW(NIM_MODIFY, &Data);
}

static void AddTrayIcon(HWND Window)
{
	NOTIFYICONDATAW Data =
	{
		.cbSize = sizeof(Data),
		.hWnd = Window,
		.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP,
		.uCallbackMessage = WM_WCAP_COMMAND,
		.hIcon = gIcon1,
	};
	StrCpyNW(Data.szTip, WCAP_TITLE, _countof(Data.szTip));
	Shell_NotifyIconW(NIM_ADD, &Data);
}

static void RemoveTrayIcon(HWND Window)
{
	NOTIFYICONDATAW Data =
	{
		.cbSize = sizeof(Data),
		.hWnd = Window,
	};
	Shell_NotifyIconW(NIM_DELETE, &Data);
}

static void ShowFileInFolder(LPCWSTR Filename)
{
	SFGAOF Flags;
	PIDLIST_ABSOLUTE List;
	if (Filename[0] && SUCCEEDED(SHParseDisplayName(Filename, NULL, &List, 0, &Flags)))
	{
		HR(SHOpenFolderAndSelectItems(List, 0, NULL, 0));
		CoTaskMemFree(List);
	}
}

static void Sound__Build(BYTE* Buffer, const SoundTone* Tones, int ToneCount)
{
	int TotalSamples = 0;
	for (int i = 0; i < ToneCount; i++)
	{
		TotalSamples += WCAP_SOUND_RATE * Tones[i].Msec / 1000;
	}

	DWORD DataSize = (DWORD)TotalSamples * 2;
	if (DataSize + 44u > (DWORD)WCAP_SOUND_MAX_SIZE)
	{
		return;
	}

	CopyMemory(Buffer + 0, "RIFF", 4);
	*(DWORD*)(Buffer + 4) = 36 + DataSize;
	CopyMemory(Buffer + 8, "WAVEfmt ", 8);
	*(DWORD*)(Buffer + 16) = 16;
	*(WORD*)(Buffer + 20) = 1; // PCM
	*(WORD*)(Buffer + 22) = 1; // mono
	*(DWORD*)(Buffer + 24) = WCAP_SOUND_RATE;
	*(DWORD*)(Buffer + 28) = WCAP_SOUND_RATE * 2;
	*(WORD*)(Buffer + 32) = 2;  // block align
	*(WORD*)(Buffer + 34) = 16; // bits per sample
	CopyMemory(Buffer + 36, "data", 4);
	*(DWORD*)(Buffer + 40) = DataSize;

	short* Samples = (short*)(Buffer + 44);
	double Phase = 0.0;
	const double TwoPi = 6.283185307179586;

	for (int t = 0; t < ToneCount; t++)
	{
		int Count = WCAP_SOUND_RATE * Tones[t].Msec / 1000;
		double Step = TwoPi * Tones[t].Freq / WCAP_SOUND_RATE;
		int Fade = WCAP_SOUND_RATE / 200; // 5 msec fade to avoid clicks

		for (int i = 0; i < Count; i++)
		{
			double Envelope = 1.0;
			if (i < Fade)
			{
				Envelope = (double)i / Fade;
			}
			else if (i > Count - Fade)
			{
				Envelope = (double)(Count - i) / Fade;
			}

			*Samples++ = (short)(9000.0 * Envelope * sin(Phase));
			Phase += Step;
			if (Phase >= TwoPi)
			{
				Phase -= TwoPi;
			}
		}
	}
}

static void Sound_Init(void)
{
	Sound__Build(gSoundStart, gSoundTonesStart, (int)ARRAYSIZE(gSoundTonesStart));
	Sound__Build(gSoundStop,  gSoundTonesStop,  (int)ARRAYSIZE(gSoundTonesStop));
}

static void Sound_Play(BOOL Start)
{
	if (!gConfig.PlaySounds)
	{
		return;
	}
	// SND_ASYNC keeps capture thread free, buffers stay valid for whole process lifetime
	PlaySoundW((LPCWSTR)(Start ? gSoundStart : gSoundStop), NULL, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

static BOOL Filename__Invalid(WCHAR Character)
{
	if (Character == WCAP_SEQ_MARKER)
	{
		return FALSE;
	}
	if (Character < 32)
	{
		return TRUE;
	}
	return StrChrW(L"<>:\"/\\|?*", Character) != NULL;
}

static void Filename__Sanitize(WCHAR* Text)
{
	for (WCHAR* Pos = Text; *Pos; Pos++)
	{
		if (Filename__Invalid(*Pos))
		{
			*Pos = L'-';
		}
	}

	size_t Length = wcslen(Text);
	while (Length > 0 && (Text[Length - 1] == L' ' || Text[Length - 1] == L'.'))
	{
		Text[--Length] = 0;
	}
}

static void Filename__Append(WCHAR* Out, size_t Capacity, size_t* Length, LPCWSTR Text)
{
	while (*Text && *Length + 1 < Capacity)
	{
		Out[(*Length)++] = *Text++;
	}
	Out[*Length] = 0;
}

static void Filename__Expand(LPCWSTR Template, const SYSTEMTIME* Time, UINT Seq, WCHAR* Out, size_t Capacity)
{
	size_t Length = 0;
	Out[0] = 0;

	for (LPCWSTR Pos = Template; *Pos; )
	{
		if (*Pos == L'{')
		{
			LPCWSTR End = StrChrW(Pos + 1, L'}');
			if (End != NULL && End > Pos + 1)
			{
				size_t TokenLength = (size_t)(End - Pos - 1);
				WCHAR Value[48];
				BOOL Handled = TRUE;

				#define WCAP_TOKEN(Text) (TokenLength == _countof(Text) - 1 && StrCmpNIW(Pos + 1, Text, (int)TokenLength) == 0)

				if (WCAP_TOKEN(L"yyyy"))
				{
					StrFormat(Value, L"%04u", Time->wYear);
				}
				else if (WCAP_TOKEN(L"yy"))
				{
					StrFormat(Value, L"%02u", Time->wYear % 100);
				}
				else if (WCAP_TOKEN(L"MM"))
				{
					StrFormat(Value, L"%02u", Time->wMonth);
				}
				else if (WCAP_TOKEN(L"dd"))
				{
					StrFormat(Value, L"%02u", Time->wDay);
				}
				else if (WCAP_TOKEN(L"HH"))
				{
					StrFormat(Value, L"%02u", Time->wHour);
				}
				else if (WCAP_TOKEN(L"mm"))
				{
					StrFormat(Value, L"%02u", Time->wMinute);
				}
				else if (WCAP_TOKEN(L"ss"))
				{
					StrFormat(Value, L"%02u", Time->wSecond);
				}
				else if (WCAP_TOKEN(L"date"))
				{
					StrFormat(Value, L"%04u-%02u-%02u", Time->wYear, Time->wMonth, Time->wDay);
				}
				else if (WCAP_TOKEN(L"time"))
				{
					StrFormat(Value, L"%02u-%02u-%02u", Time->wHour, Time->wMinute, Time->wSecond);
				}
				else if (WCAP_TOKEN(L"datetime"))
				{
					StrFormat(Value, L"%04u-%02u-%02u_%02u-%02u-%02u",
						Time->wYear, Time->wMonth, Time->wDay, Time->wHour, Time->wMinute, Time->wSecond);
				}
				else if (WCAP_TOKEN(L"seq") || WCAP_TOKEN(L"index") || WCAP_TOKEN(L"n"))
				{
					if (Seq == 0)
					{
						Value[0] = WCAP_SEQ_MARKER;
						Value[1] = 0;
					}
					else
					{
						StrFormat(Value, L"%02u", Seq);
					}
				}
				else
				{
					Handled = FALSE;
				}

				#undef WCAP_TOKEN

				if (Handled)
				{
					Filename__Append(Out, Capacity, &Length, Value);
					Pos = End + 1;
					continue;
				}
			}
		}

		WCHAR Character[2] = { *Pos++, 0 };
		Filename__Append(Out, Capacity, &Length, Character);
	}
}

static UINT Filename__NextSequence(LPCWSTR Folder, LPCWSTR Prefix, LPCWSTR Suffix)
{
	WCHAR Pattern[MAX_PATH];
	StrFormat(Pattern, L"%ls\\%ls*%ls.mp4", Folder, Prefix, Suffix);

	WIN32_FIND_DATAW Data;
	HANDLE Find = FindFirstFileW(Pattern, &Data);
	if (Find == INVALID_HANDLE_VALUE)
	{
		return 1;
	}

	UINT Next = 1;
	size_t PrefixLength = wcslen(Prefix);
	size_t SuffixLength = wcslen(Suffix);
	const size_t ExtensionLength = 4; // ".mp4"

	do
	{
		LPCWSTR Name = Data.cFileName;
		size_t NameLength = wcslen(Name);
		if (NameLength <= PrefixLength + SuffixLength + ExtensionLength)
		{
			continue;
		}
		if (StrCmpNIW(Name, Prefix, (int)PrefixLength) != 0)
		{
			continue;
		}
		if (StrCmpIW(Name + NameLength - ExtensionLength, L".mp4") != 0)
		{
			continue;
		}
		if (SuffixLength != 0 && StrCmpNIW(Name + NameLength - SuffixLength - ExtensionLength, Suffix, (int)SuffixLength) != 0)
		{
			continue;
		}

		size_t DigitCount = NameLength - PrefixLength - SuffixLength - ExtensionLength;
		LPCWSTR Digits = Name + PrefixLength;
		UINT Value = 0;
		BOOL Valid = DigitCount > 0;

		for (size_t i = 0; i < DigitCount; i++)
		{
			if (Digits[i] < L'0' || Digits[i] > L'9')
			{
				Valid = FALSE;
				break;
			}
			if (Value < 100000u)
			{
				Value = Value * 10 + (UINT)(Digits[i] - L'0');
			}
		}

		if (Valid && Value >= Next)
		{
			Next = Value + 1;
		}
	}
	while (FindNextFileW(Find, &Data));
	FindClose(Find);

	return Next;
}

static void BuildRecordingPath(const SYSTEMTIME* Time)
{
	WCHAR Template[WCAP_FILENAME_TEMPLATE_MAX];
	LPCWSTR Source = gConfig.FilenameTemplate[0] ? gConfig.FilenameTemplate : WCAP_DEFAULT_FILENAME_TEMPLATE;
	StrCpyNW(Template, Source, _countof(Template));

	WCHAR Stem[WCAP_FILENAME_MAX];
	Filename__Expand(Template, Time, 0, Stem, _countof(Stem));
	Filename__Sanitize(Stem);

	WCHAR Name[WCAP_FILENAME_MAX];

	LPCWSTR Marker = StrChrW(Stem, WCAP_SEQ_MARKER);
	if (Marker)
	{
		WCHAR Prefix[WCAP_FILENAME_MAX];
		StrCpyNW(Prefix, Stem, (int)(Marker - Stem) + 1);

		UINT Sequence = Filename__NextSequence(gConfig.OutputFolder, Prefix, Marker + 1);
		Filename__Expand(Template, Time, Sequence, Name, _countof(Name));
		Filename__Sanitize(Name);
	}
	else
	{
		WCHAR Path[MAX_PATH];
		StrFormat(Path, L"%ls\\%ls.mp4", gConfig.OutputFolder, Stem);

		if (GetFileAttributesW(Path) == INVALID_FILE_ATTRIBUTES)
		{
			StrCpyW(Name, Stem);
		}
		else
		{
			// no {seq} in template, but such file already exists - append sequence number
			WCHAR Prefix[WCAP_FILENAME_MAX];
			StrFormat(Prefix, L"%ls-", Stem);

			UINT Sequence = Filename__NextSequence(gConfig.OutputFolder, Prefix, L"");
			StrFormat(Name, L"%ls%02u", Prefix, Sequence);
		}
	}

	if (Name[0] == 0)
	{
		StrCpyW(Name, L"video");
	}

	StrFormat(gRecordingPath, L"%ls\\%ls.mp4", gConfig.OutputFolder, Name);
}

static void StartRecording(ID3D11Device* Device, HWND Window)
{
	SYSTEMTIME Time;
	GetLocalTime(&Time);

	int Error = SHCreateDirectoryExW(NULL, gConfig.OutputFolder, NULL);
	if (Error != ERROR_SUCCESS && Error != ERROR_FILE_EXISTS && Error != ERROR_ALREADY_EXISTS)
	{
		ShowNotification(L"无法创建输出文件夹！", L"无法开始录制", NIIF_WARNING);
		ScreenCapture_Stop(&gCapture);
		ID3D11Device_Release(Device);
		return;
	}

	BuildRecordingPath(&Time);

	DWM_TIMING_INFO Info = { .cbSize = sizeof(Info) };
	HR(DwmGetCompositionTimingInfo(NULL, &Info));

	DWORD FramerateNum = Info.rateCompose.uiNumerator;
	DWORD FramerateDen = Info.rateCompose.uiDenominator;
	if (gConfig.VideoMaxFramerate > 0 && gConfig.VideoMaxFramerate * FramerateDen < FramerateNum)
	{
		// limit rate only if max framerate is specified and it is lower than compositor framerate
		gRecordingLimitFramerate = gConfig.VideoMaxFramerate;
		FramerateNum = gConfig.VideoMaxFramerate;
		FramerateDen = 1;
	}
	else
	{
		gRecordingLimitFramerate = 0;
	}

	EncoderConfig EncConfig =
	{
		.Width = gCapture.Rect.right - gCapture.Rect.left,
		.Height = gCapture.Rect.bottom - gCapture.Rect.top,
		.FramerateNum = FramerateNum,
		.FramerateDen = FramerateDen,
		.Config = &gConfig,
	};

	if (gConfig.CaptureAudio)
	{
		HWND ApplicationWindow = gConfig.ApplicationLocalAudio && AudioCapture_CanCaptureApplicationLocal() ? Window : NULL;
		if (!AudioCapture_Start(&gAudio, ApplicationWindow))
		{
			ShowNotification(L"无法捕获音频！", L"无法开始录制", NIIF_WARNING);
			ScreenCapture_Stop(&gCapture);
			ID3D11Device_Release(Device);
			return;
		}
		EncConfig.AudioFormat = gAudio.Format;
	}

	if (!Encoder_Start(&gEncoder, Device, gRecordingPath, &EncConfig))
	{
		if (gConfig.CaptureAudio)
		{
			AudioCapture_Stop(&gAudio);
		}
		ScreenCapture_Stop(&gCapture);
		ID3D11Device_Release(Device);
		return;
	}

	gRecordingNextTooltip = 0;
	gRecordingNextEncode = 0;
	gRecordingLastFrame = 0;
	gRecordingDroppedFrames = 0;
	ScreenCapture_Start(&gCapture, gConfig.MouseCursor, gConfig.ShowRecordingBorder, gConfig.IncludeSecondaryWindows);

	if (gConfig.CaptureAudio)
	{
		SetTimer(gWindow, WCAP_AUDIO_CAPTURE_TIMER, WCAP_AUDIO_CAPTURE_INTERVAL, NULL);
	}
	SetTimer(gWindow, WCAP_VIDEO_UPDATE_TIMER, WCAP_VIDEO_UPDATE_INTERVAL, NULL);

	UpdateTrayIcon(gIcon2);
	gRecordingState = SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED);
	gRecording = TRUE;

	Sound_Play(TRUE);

	ID3D11Device_Release(Device);
}

static void EncodeCapturedAudio(void)
{
	if (gEncoder.StartTime == 0)
	{
		// we don't know when first video frame starts yet
		return;
	}

	AudioCaptureData Data;
	while (AudioCapture_GetData(&gAudio, &Data, gEncoder.StartTime))
	{
		UINT32 FramesToEncode = (UINT32)Data.Count;
		if (Data.Time < gEncoder.StartTime)
		{
			const UINT32 SampleRate = gAudio.Format->nSamplesPerSec;
			const UINT32 BytesPerFrame = gAudio.Format->nBlockAlign;

			// figure out how much time (100nsec units) and frame count to skip from current buffer
			UINT64 TimeToSkip = gEncoder.StartTime - Data.Time;
			UINT32 FramesToSkip = (UINT32)((TimeToSkip * SampleRate - 1) / MF_UNITS_PER_SECOND + 1);
			if (FramesToSkip < FramesToEncode)
			{
				// need to skip part of captured data
				Data.Time += FramesToSkip * MF_UNITS_PER_SECOND / SampleRate;
				FramesToEncode -= FramesToSkip;
				if (Data.Samples)
				{
					Data.Samples = (BYTE*)Data.Samples + FramesToSkip * BytesPerFrame;
				}
			}
			else
			{
				// need to skip all of captured data
				FramesToEncode = 0;
			}
		}
		if (FramesToEncode != 0)
		{
			Assert(Data.Time >= gEncoder.StartTime);
			Encoder_NewSamples(&gEncoder, Data.Samples, FramesToEncode, Data.Time, gTickFreq.QuadPart);
		}
		AudioCapture_ReleaseData(&gAudio, &Data);
	}
}

static void StopRecording(void)
{
	BOOL WasRecording = gRecording;

	gRecording = FALSE;
	SetThreadExecutionState(gRecordingState);

	if (gConfig.CaptureAudio)
	{
		KillTimer(gWindow, WCAP_AUDIO_CAPTURE_TIMER);
		AudioCapture_Flush(&gAudio);
		EncodeCapturedAudio();
		AudioCapture_Stop(&gAudio);
	}
	KillTimer(gWindow, WCAP_VIDEO_UPDATE_TIMER);

	ScreenCapture_Stop(&gCapture);
	Encoder_Stop(&gEncoder);
	if (gConfig.OpenFolder)
	{
		ShowFileInFolder(gRecordingPath);
	}

	SetWindowPos(gWindow, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE);
	SetWindowLongW(gWindow, GWL_EXSTYLE, 0);

	UpdateTrayIcon(gIcon1);
	UpdateTrayTitle(WCAP_TITLE);

	if (WasRecording)
	{
		Sound_Play(FALSE);
	}
}

static ID3D11Device* CreateDevice(void)
{
	IDXGIAdapter* Adapter = NULL;

	if (gConfig.HardwareEncoder)
	{
		IDXGIFactory* Factory;
		if (SUCCEEDED(CreateDXGIFactory(&IID_IDXGIFactory, (void**)&Factory)))
		{
			IDXGIFactory6* Factory6;
			if (SUCCEEDED(IDXGIFactory_QueryInterface(Factory, &IID_IDXGIFactory6, (void**)&Factory6)))
			{
				DXGI_GPU_PREFERENCE Preference = gConfig.HardwarePreferIntegrated ? DXGI_GPU_PREFERENCE_MINIMUM_POWER : DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE;
				if (FAILED(IDXGIFactory6_EnumAdapterByGpuPreference(Factory6, 0, Preference, &IID_IDXGIAdapter, &Adapter)))
				{
					// just to be safe
					Adapter = NULL;
				}
				IDXGIFactory6_Release(Factory6);
			}
			IDXGIFactory_Release(Factory);
		}
	}

	ID3D11Device* Device;

	UINT flags = 0;
#ifndef NDEBUG
	flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
	// if adapter is selected then driver type must be unknown
	D3D_DRIVER_TYPE Driver = Adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE;
	if (FAILED(D3D11CreateDevice(Adapter, Driver, NULL, flags, (D3D_FEATURE_LEVEL[]) { D3D_FEATURE_LEVEL_11_0 }, 1, D3D11_SDK_VERSION, &Device, NULL, NULL)))
	{
		ShowNotification(L"无法创建 D3D11 设备！", L"错误", NIIF_ERROR);
		Device = NULL;
	}
	if (Adapter)
	{
		IDXGIAdapter_Release(Adapter);
	}

	if (flags & D3D11_CREATE_DEVICE_DEBUG)
	{
		ID3D11InfoQueue* Info;
		if (SUCCEEDED(ID3D11Device_QueryInterface(Device, &IID_ID3D11InfoQueue, &Info)))
		{
			ID3D11InfoQueue_SetBreakOnSeverity(Info, D3D11_MESSAGE_SEVERITY_CORRUPTION, TRUE);
			ID3D11InfoQueue_SetBreakOnSeverity(Info, D3D11_MESSAGE_SEVERITY_ERROR, TRUE);
			ID3D11InfoQueue_Release(Info);
		}
	}

	return Device;
}

static void CaptureWindow(void)
{
	HWND Window = GetForegroundWindow();
	if (Window == NULL)
	{
		ShowNotification(L"没有选中任何窗口！", L"无法开始录制", NIIF_WARNING);
		return;
	}

	// figure out who is owner of child window if somehow child window is selected (happens for fancy winamp skins)
	HWND Parent = GetParent(Window);
	while (Parent != NULL)
	{
		Window = Parent;
		Parent = GetParent(Window);
	}

	DWORD Affinity;
	BOOL Success = GetWindowDisplayAffinity(Window, &Affinity);
	Assert(Success);

	if (Affinity != WDA_NONE)
	{
		ShowNotification(L"该窗口不允许被捕获！", L"无法开始录制", NIIF_WARNING);
		return;
	}

	LONG ExStyle = GetWindowLongW(Window, GWL_EXSTYLE);
	if (ExStyle & WS_EX_TOOLWINDOW)
	{
		ShowNotification(L"无法捕获工具窗口！", L"无法开始录制", NIIF_WARNING);
		return;
	}

	ID3D11Device* Device = CreateDevice();
	if (!Device)
	{
		return;
	}

	if (!ScreenCapture_CreateForWindow(&gCapture, Device, Window, gConfig.OnlyClientArea, !gConfig.KeepRoundedWindowCorners))
	{
		ID3D11Device_Release(Device);
		ShowNotification(L"无法录制所选窗口！", L"错误", NIIF_WARNING);
		return;
	}

	StartRecording(Device, Window);
}

static void CaptureMonitor(void)
{
	POINT Mouse;
	GetCursorPos(&Mouse);

	HMONITOR Monitor = MonitorFromPoint(Mouse, MONITOR_DEFAULTTONULL);
	if (Monitor == NULL)
	{
		ShowNotification(L"未找到显示器！", L"无法开始录制", NIIF_WARNING);
		return;
	}

	ID3D11Device* Device = CreateDevice();
	if (!Device)
	{
		return;
	}

	if (!ScreenCapture_CreateForMonitor(&gCapture, Device, Monitor, NULL))
	{
		ShowNotification(L"无法录制所选显示器！", L"错误", NIIF_WARNING);
		return;
	}

	StartRecording(Device, NULL);
}

static void CaptureRegionInit(void)
{
	POINT Mouse;
	GetCursorPos(&Mouse);

	HMONITOR Monitor = MonitorFromPoint(Mouse, MONITOR_DEFAULTTONULL);
	if (Monitor == NULL)
	{
		ShowNotification(L"未找到显示器！", L"无法开始录制", NIIF_WARNING);
		return;
	}

	MONITORINFOEXW Info = { .cbSize = sizeof(Info) };
	GetMonitorInfoW(Monitor, (LPMONITORINFO)&Info);

	HDC DeviceContext = CreateDCW(L"DISPLAY", Info.szDevice, NULL, NULL);
	if (DeviceContext == NULL)
	{
		ShowNotification(L"获取显示器画面失败！", L"无法开始录制", NIIF_WARNING);
		return;
	}

	DWORD Width = Info.rcMonitor.right - Info.rcMonitor.left;
	DWORD Height = Info.rcMonitor.bottom - Info.rcMonitor.top;

	// capture image from desktop

	HDC MemoryContext = CreateCompatibleDC(DeviceContext);
	Assert(MemoryContext);

	HBITMAP MemoryBitmap = CreateCompatibleBitmap(DeviceContext, Width, Height);
	Assert(MemoryBitmap);

	SelectObject(MemoryContext, MemoryBitmap);
	BitBlt(MemoryContext, 0, 0, Width, Height, DeviceContext, 0, 0, SRCCOPY);

	// prepare darkened image by doing alpha blend

	HDC MemoryDarkContext = CreateCompatibleDC(DeviceContext);
	Assert(MemoryDarkContext);

	HBITMAP MemoryDarkBitmap = CreateCompatibleBitmap(DeviceContext, Width, Height);
	Assert(MemoryDarkBitmap);

	BLENDFUNCTION Blend =
	{
		.BlendOp = AC_SRC_OVER,
		.SourceConstantAlpha = 0x40,
	};

	SelectObject(MemoryDarkContext, MemoryDarkBitmap);
	AlphaBlend(MemoryDarkContext, 0, 0, Width, Height, MemoryContext, 0, 0, Width, Height, Blend);

	// done

	DeleteDC(DeviceContext);

	gRectMonitor = Monitor;
	gRectContext = MemoryContext;
	gRectDarkContext = MemoryDarkContext;
	gRectBitmap = MemoryBitmap;
	gRectDarkBitmap = MemoryDarkBitmap;
	gRectWidth = Width;
	gRectHeight = Height;
	gRectSelected = FALSE;
	gRectResize = WCAP_RESIZE_NONE;
	gRectSetSize[0] = gRectSetSize[1] = 0;
	gRectSetSizeClick = FALSE;

	SetCursor(gCursorResize[WCAP_RESIZE_NONE]);
	SetWindowPos(gWindow, HWND_TOPMOST, Info.rcMonitor.left, Info.rcMonitor.top, Width, Height, SWP_SHOWWINDOW);
	SetForegroundWindow(gWindow);
	InvalidateRect(gWindow, NULL, FALSE);
}

static void CaptureRegionRelease(void)
{
	SetWindowPos(gWindow, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE);
	SetWindowLongW(gWindow, GWL_EXSTYLE, 0);

	if (gRectContext)
	{
		DeleteDC(gRectContext);
		gRectContext = NULL;

		DeleteObject(gRectBitmap);
		gRectBitmap = NULL;

		DeleteDC(gRectDarkContext);
		gRectDarkContext = NULL;

		DeleteObject(gRectDarkBitmap);
		gRectDarkBitmap = NULL;
	}
}

static void CaptureRegionDone(void)
{
	SetCursor(gCursorArrow);
	ReleaseCapture();

	CaptureRegionRelease();
}

static void CaptureRegion(void)
{
	CaptureRegionDone();

	MONITORINFO Info = { .cbSize = sizeof(Info) };
	GetMonitorInfoW(gRectMonitor, &Info);

	RECT Rect =
	{
		.left   = gRectSelection[0].x,
		.right  = gRectSelection[1].x,
		.top    = gRectSelection[0].y,
		.bottom = gRectSelection[1].y,
	};

	LONG ExStyle = WS_EX_LAYERED | WS_EX_TRANSPARENT;
	SetWindowLongW(gWindow, GWL_EXSTYLE, ExStyle);
	SetLayeredWindowAttributes(gWindow, RGB(255, 0, 255), 0, LWA_COLORKEY);

	ID3D11Device* Device = CreateDevice();
	if (!Device)
	{
		CaptureRegionRelease();
		return;
	}

	if (!ScreenCapture_CreateForMonitor(&gCapture, Device, gRectMonitor, &Rect))
	{
		ShowNotification(L"无法录制显示器！", L"错误", NIIF_WARNING);
		CaptureRegionRelease();
		return;
	}

	StartRecording(Device, NULL);

	if (gRecording)
	{
		int X = Info.rcMonitor.left + Rect.left - (WCAP_RECT_BORDER + 1);
		int Y = Info.rcMonitor.top + Rect.top - (WCAP_RECT_BORDER + 1);
		int W = Rect.right - Rect.left + 2 * (WCAP_RECT_BORDER + 1);
		int H = Rect.bottom - Rect.top + 2 * (WCAP_RECT_BORDER + 1);
		SetWindowPos(gWindow, HWND_TOPMOST, X, Y, W, H, SWP_SHOWWINDOW);
		InvalidateRect(gWindow, NULL, FALSE);
	}
	else
	{
		CaptureRegionRelease();
	}
}

static int GetPointResize(int X, int Y)
{
	int BorderX = GetSystemMetrics(SM_CXSIZEFRAME);
	int BorderY = GetSystemMetrics(SM_CYSIZEFRAME);

	int X0 = min(gRectSelection[0].x, gRectSelection[1].x);
	int Y0 = min(gRectSelection[0].y, gRectSelection[1].y);
	int X1 = max(gRectSelection[0].x, gRectSelection[1].x);
	int Y1 = max(gRectSelection[0].y, gRectSelection[1].y);

	POINT P = { X, Y };

	RECT TL = { X0 - BorderX, Y0 - BorderY, X0 + BorderX, Y0 + BorderY };
	if (PtInRect(&TL, P)) return WCAP_RESIZE_TL;

	RECT TR = { X1 - BorderX, Y0 - BorderY, X1 + BorderX, Y0 + BorderY };
	if (PtInRect(&TR, P)) return WCAP_RESIZE_TR;

	RECT BL = { X0 - BorderX, Y1 - BorderY, X0 + BorderX, Y1 + BorderY };
	if (PtInRect(&BL, P)) return WCAP_RESIZE_BL;

	RECT BR = { X1 - BorderX, Y1 - BorderY, X1 + BorderX, Y1 + BorderY };
	if (PtInRect(&BR, P)) return WCAP_RESIZE_BR;

	RECT T = { X0, Y0 - BorderY, X1, Y0 + BorderY };
	if (PtInRect(&T, P)) return WCAP_RESIZE_T;

	RECT B = { X0, Y1 - BorderY, X1, Y1 + BorderY };
	if (PtInRect(&B, P)) return WCAP_RESIZE_B;

	RECT L = { X0 - BorderX, Y0, X0 + BorderX, Y1 };
	if (PtInRect(&L, P)) return WCAP_RESIZE_L;

	RECT R = { X1 - BorderX, Y0, X1 + BorderX, Y1 };
	if (PtInRect(&R, P)) return WCAP_RESIZE_R;

	RECT M = { X0, Y0, X1, Y1 };
	if (PtInRect(&M, P)) return WCAP_RESIZE_M;

	return WCAP_RESIZE_NONE;
}

void DisableHotKeys(void)
{
	for (int i = 1; i <= HOT_COUNT; i++)
	{
		UnregisterHotKey(gWindow, i);
	}
}

BOOL EnableHotKeys(void)
{
	BOOL Success = TRUE;

	const DWORD Shortcuts[HOT_COUNT] =
	{
		gConfig.ShortcutWindow,
		gConfig.ShortcutMonitor,
		gConfig.ShortcutRegion,
		gConfig.ShortcutToggle,
		gConfig.ShortcutStop,
	};

	for (int i = 0; i < HOT_COUNT; i++)
	{
		if (Shortcuts[i] == 0)
		{
			continue;
		}

		// same combination assigned to multiple actions - register only first one
		BOOL Duplicate = FALSE;
		for (int j = 0; j < i; j++)
		{
			if (Shortcuts[j] == Shortcuts[i])
			{
				Duplicate = TRUE;
			}
		}
		if (Duplicate)
		{
			continue;
		}

		if (!RegisterHotKey(gWindow, i + 1, HOT_GET_MOD(Shortcuts[i]), HOT_GET_KEY(Shortcuts[i])))
		{
			Success = FALSE;
		}
	}

	return Success;
}

static void AdjustRectSizeMultipleOf2(int Adjust, int Ref)
{
	int W = gRectSelection[Ref].x - gRectSelection[Adjust].x;
	W = (W + (W > 0)) & ~1;
	gRectSelection[Adjust].x = gRectSelection[Ref].x - W;

	int H = gRectSelection[Ref].y - gRectSelection[Adjust].y;
	H = (H + (H > 0)) & ~1;
	gRectSelection[Adjust].y = gRectSelection[Ref].y - H;
}

static LRESULT CALLBACK WindowProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
	if (Message == WM_CREATE)
	{
		HR(BufferedPaintInit());
		AddTrayIcon(Window);
		return 0;
	}
	else if (Message == WM_DESTROY)
	{
		if (gRecording)
		{
			StopRecording();
		}
		RemoveTrayIcon(Window);
		PostQuitMessage(0);
		return 0;
	}
	else if (Message == WM_CLOSE)
	{
		if (gRectContext)
		{
			CaptureRegionDone();
		}
		return 0;
	}
	else if (Message == WM_ACTIVATEAPP)
	{
		if (gRectContext)
		{
			if (WParam == FALSE)
			{
				CaptureRegionDone();
				return 0;
			}
		}
	}
	else if (Message == WM_KEYDOWN)
	{
		if (gRectContext)
		{
			if (WParam == VK_ESCAPE)
			{
				CaptureRegionDone();
				return 0;
			}
			else if (WParam == VK_RETURN)
			{
				if (gRectSelected)
				{
					CaptureRegion();
				}
				return 0;
			}
		}
	}
	else if (Message == WM_LBUTTONDOWN)
	{
		if (gRectContext)
		{
			if (gRectSetSize[0])
			{
				gRectSetSizeClick = TRUE;
				gRectSelection[1].x = gRectSelection[0].x + gRectSetSize[0];
				gRectSelection[1].y = gRectSelection[0].y + gRectSetSize[1];
				InvalidateRect(Window, NULL, FALSE);
			}
			else
			{
				int X = GET_X_LPARAM(LParam);
				int Y = GET_Y_LPARAM(LParam);

				int Resize = gRectSelected ? GetPointResize(X, Y) : WCAP_RESIZE_NONE;
				if (Resize == WCAP_RESIZE_NONE)
				{
					// inital rectangle will be empty
					gRectSelection[0].x = gRectSelection[1].x = X;
					gRectSelection[0].y = gRectSelection[1].y = Y;
					gRectSelected = FALSE;

					InvalidateRect(Window, NULL, FALSE);
				}
				else
				{
					// resizing direction
					gRectMousePos = (POINT){ X, Y };
				}

				gRectResize = Resize;
				SetCapture(Window);
			}
			return 0;
		}
	}
	else if (Message == WM_LBUTTONUP)
	{
		if (gRectContext)
		{
			if (gRectSetSizeClick)
			{
				gRectSetSizeClick = FALSE;
			}
			else
			{
				if (gRectSelected)
				{
					// fix the selected rectangle coordinates, so next resizing starts on the correct side
					int X0 = min(gRectSelection[0].x, gRectSelection[1].x);
					int Y0 = min(gRectSelection[0].y, gRectSelection[1].y);
					int X1 = max(gRectSelection[0].x, gRectSelection[1].x);
					int Y1 = max(gRectSelection[0].y, gRectSelection[1].y);
					gRectSelection[0] = (POINT){ X0, Y0 };
					gRectSelection[1] = (POINT){ X1, Y1 };
				}
				ReleaseCapture();
			}
			return 0;
		}
	}
	else if (Message == WM_MOUSEMOVE)
	{
		if (gRectContext)
		{
			int X = GET_X_LPARAM(LParam);
			int Y = GET_Y_LPARAM(LParam);

			if (gRectSetSize[0])
			{
				SetCursor(gCursorClick);
				InvalidateRect(Window, NULL, FALSE);
			}
			else if (gRectSetSizeClick)
			{
				InvalidateRect(Window, NULL, FALSE);
			}
			else if (WParam & MK_LBUTTON)
			{
				BOOL Update = FALSE;

				if (gRectResize == WCAP_RESIZE_TL || gRectResize == WCAP_RESIZE_L || gRectResize == WCAP_RESIZE_BL)
				{
					// left moved
					gRectSelection[0].x = X;
					AdjustRectSizeMultipleOf2(0, 1);
					Update = TRUE;
				}
				else if (gRectResize == WCAP_RESIZE_TR || gRectResize == WCAP_RESIZE_R || gRectResize == WCAP_RESIZE_BR)
				{
					// right moved
					gRectSelection[1].x = X;
					AdjustRectSizeMultipleOf2(1, 0);
					Update = TRUE;
				}

				if (gRectResize == WCAP_RESIZE_TL || gRectResize == WCAP_RESIZE_T || gRectResize == WCAP_RESIZE_TR)
				{
					// top moved
					gRectSelection[0].y = Y;
					AdjustRectSizeMultipleOf2(0, 1);
					Update = TRUE;
				}
				else if (gRectResize == WCAP_RESIZE_BL || gRectResize == WCAP_RESIZE_B || gRectResize == WCAP_RESIZE_BR)
				{
					// bottom moved
					gRectSelection[1].y = Y;
					AdjustRectSizeMultipleOf2(1, 0);
					Update = TRUE;
				}

				if (gRectResize == WCAP_RESIZE_M)
				{
					// if moving whole rectangle update both
					int DX = X - gRectMousePos.x;
					int DY = Y - gRectMousePos.y;
					gRectMousePos = (POINT){ X, Y };

					gRectSelection[0].x += DX;
					gRectSelection[0].y += DY;
					gRectSelection[1].x += DX;
					gRectSelection[1].y += DY;

					Update = TRUE;
				}
				else if (gRectResize == WCAP_RESIZE_NONE)
				{
					// no resize means we're selecting initial rectangle
					gRectSelection[1].x = X;
					gRectSelection[1].y = Y;
					AdjustRectSizeMultipleOf2(1, 0);
					if (gRectSelection[0].x != gRectSelection[1].x && gRectSelection[0].y != gRectSelection[1].y)
					{
						// when we have non-zero size rectangle, we're good with initial stage
						gRectSelected = TRUE;
						Update = TRUE;
					}
				}

				if (Update)
				{
					InvalidateRect(Window, NULL, FALSE);
				}
			}
			else
			{
				int Resize = gRectSelected ? GetPointResize(X, Y) : WCAP_RESIZE_NONE;
				SetCursor(gCursorResize[Resize]);

				if (Resize == WCAP_RESIZE_NONE)
				{
					// in case hovering over resize text
					InvalidateRect(Window, NULL, FALSE);
				}
			}

			return 0;
		}
	}
	else if (Message == WM_TIMER)
	{
		if (gRecording)
		{
			if (WParam == WCAP_AUDIO_CAPTURE_TIMER)
			{
				EncodeCapturedAudio();
				return 0;
			}
			else if (WParam == WCAP_VIDEO_UPDATE_TIMER)
			{
				LARGE_INTEGER Time;
				QueryPerformanceCounter(&Time);
				Encoder_Update(&gEncoder, Time.QuadPart, gTickFreq.QuadPart);
				return 0;
			}
		}
	}
	else if (Message == WM_POWERBROADCAST)
	{
		if (WParam == PBT_APMQUERYSUSPEND)
		{
			if (gRecording)
			{
				if (LParam & 1)
				{
					// reject request to suspend when recording
					return BROADCAST_QUERY_DENY;
				}
				else
				{
					// if cannot prevent suspend, need to stop recording
					StopRecording();
				}
			}
			else
			{
				// allow to suspend when not recording
			}
		}
		return TRUE;
	}
	else if (Message == WM_WCAP_COMMAND)
	{
		if (LOWORD(LParam) == WM_RBUTTONUP)
		{
			HMENU Menu = CreatePopupMenu();
			Assert(Menu);

			AppendMenuW(Menu, MF_STRING, CMD_WCAP, WCAP_TITLE);
			AppendMenuW(Menu, MF_SEPARATOR, 0, NULL);
			AppendMenuW(Menu, MF_STRING | (gRecording ? MF_DISABLED : 0), CMD_SETTINGS, L"设置(&S)...");
			AppendMenuW(Menu, MF_STRING, CMD_QUIT, L"退出(&X)");

			POINT Mouse;
			GetCursorPos(&Mouse);

			SetForegroundWindow(Window);
			int Command = TrackPopupMenu(Menu, TPM_RETURNCMD | TPM_NONOTIFY, Mouse.x, Mouse.y, 0, Window, NULL);
			if (Command == CMD_WCAP)
			{
				ShellExecuteW(NULL, L"open", WCAP_URL, NULL, NULL, SW_SHOWNORMAL);
			}
			else if (Command == CMD_QUIT)
			{
				DestroyWindow(Window);
			}
			else if (Command == CMD_SETTINGS)
			{
				if (Config_ShowDialog(&gConfig))
				{
					Config_Save(&gConfig, gConfigPath);
					DisableHotKeys();
					EnableHotKeys();
				}
			}

			DestroyMenu(Menu);
		}
		else if (LOWORD(LParam) == WM_LBUTTONDBLCLK)
		{
			if (!gRecording)
			{
				if (Config_ShowDialog(&gConfig))
				{
					Config_Save(&gConfig, gConfigPath);
					DisableHotKeys();
					EnableHotKeys();
				}
			}
		}
		else if (LOWORD(LParam) == NIN_BALLOONUSERCLICK)
		{
			// TODO: no idea how to prevent this happening for right-click on tray icon...
			ShowFileInFolder(gRecordingPath);
		}
		return 0;
	}
	else if (Message == WM_HOTKEY)
	{
		if (WParam == HOT_RECORD_STOP)
		{
			if (gRecording)
			{
				StopRecording();
			}
			return 0;
		}

		if (gRecording)
		{
			// any other recording hotkey stops current recording
			StopRecording();
		}
		else if (!gRecordingStarted && gRectContext == NULL)
		{
			gRecordingStarted = TRUE;
			if (WParam == HOT_RECORD_WINDOW)
			{
				CaptureWindow();
			}
			else if (WParam == HOT_RECORD_MONITOR || WParam == HOT_RECORD_TOGGLE)
			{
				// toggle starts recording of current monitor
				CaptureMonitor();
			}
			else if (WParam == HOT_RECORD_REGION)
			{
				CaptureRegionInit();
			}
			gRecordingStarted = FALSE;
		}
		return 0;
	}
	else if (Message == WM_WCAP_TRAY_TITLE)
	{
		if (gRecording)
		{
			UINT64 FileSize;
			DWORD Bitrate, LengthMsec;
			Encoder_GetStats(&gEncoder, &Bitrate, &LengthMsec, &FileSize);

			WCHAR LengthText[128];
			StrFromTimeIntervalW(LengthText, _countof(LengthText), LengthMsec, 6);

			WCHAR SizeText[128];
			StrFormatByteSizeW(FileSize, SizeText, _countof(SizeText));

			WCHAR Text[1024];
			StrFormat(Text, L"正在录制：%dx%d @ %.2f\n时长：%ls\n码率：%u kbit/s\n大小：%ls\n丢帧：%u",
				gEncoder.OutputWidth, gEncoder.OutputHeight,
				(float)gEncoder.FramerateNum / (float)gEncoder.FramerateDen,
				LengthText,
				Bitrate,
				SizeText,
				gRecordingDroppedFrames);

			UpdateTrayTitle(Text);
		}
		return 0;
	}
	else if (Message == WM_WCAP_STOP_CAPTURE)
	{
		if (gRecording)
		{
			StopRecording();
		}
		return 0;
	}
	else if (Message == WM_WCAP_ALREADY_RUNNING)
	{
		ShowNotification(L"wcap 已经在运行了！", NULL, NIIF_INFO);
		return 0;
	}
	else if (Message == WM_TASKBARCREATED)
	{
		// in case taskbar was re-created (explorer.exe crashed) add our icon back
		AddTrayIcon(Window);
		return 0;
	}
	else if (Message == WM_ERASEBKGND)
	{
		return 1;
	}
	else if (Message == WM_PAINT)
	{
		PAINTSTRUCT Paint;
		HDC PaintContext = BeginPaint(Window, &Paint);

		HDC Context;
		HPAINTBUFFER BufferedPaint = BeginBufferedPaint(PaintContext, &Paint.rcPaint, BPBF_COMPATIBLEBITMAP, NULL, &Context);
		if (BufferedPaint)
		{
			if (gRectContext)
			{
				{
					int X = Paint.rcPaint.left;
					int Y = Paint.rcPaint.top;
					int W = Paint.rcPaint.right - Paint.rcPaint.left;
					int H = Paint.rcPaint.bottom - Paint.rcPaint.top;

					// draw darkened screenshot
					BitBlt(Context, X, Y, W, H, gRectDarkContext, X, Y, SRCCOPY);
				}

				if (gRectSelected)
				{
					// draw selected rectangle
					int X0 = min(gRectSelection[0].x, gRectSelection[1].x);
					int Y0 = min(gRectSelection[0].y, gRectSelection[1].y);
					int X1 = max(gRectSelection[0].x, gRectSelection[1].x);
					int Y1 = max(gRectSelection[0].y, gRectSelection[1].y);
					BitBlt(Context, X0, Y0, X1 - X0, Y1 - Y0, gRectContext, X0, Y0, SRCCOPY);

					RECT Rect = { X0 - 1, Y0 - 1, X1 + 1, Y1 + 1 };
					FrameRect(Context, &Rect, GetStockObject(WHITE_BRUSH));

					WCHAR Text[128];
					int TextLength = StrFormat(Text, L"%d x %d", X1 - X0, Y1 - Y0);

					SelectObject(Context, gFontBold);
					SetTextAlign(Context, TA_TOP | TA_RIGHT);
					SetTextColor(Context, RGB(255, 255, 255));
					SetBkMode(Context, TRANSPARENT);
					ExtTextOutW(Context, X1, Y1, 0, NULL, Text, TextLength, NULL);

					SelectObject(Context, gFontBold);
					SetTextAlign(Context, TA_BOTTOM | TA_LEFT);
					SetTextColor(Context, RGB(255, 255, 255));

					const WCHAR TextResize[] = L"快捷尺寸：";

					SIZE Size;
					GetTextExtentPoint32W(Context, TextResize, _countof(TextResize) - 1, &Size);
					ExtTextOutW(Context, X0, Y0, 0, NULL, TextResize, _countof(TextResize) - 1, NULL);

					int X = X0;
					SelectObject(Context, gFont);

					POINT CursorPos;
					GetCursorPos(&CursorPos);
					ScreenToClient(Window, &CursorPos);

					gRectSetSize[0] = gRectSetSize[1] = 0;

					int Sizes[][2] = { { 800, 600 }, { 1280, 720 }, { 1920, 1080 }, { 2560, 1440 } };
					for (int i=0; i<_countof(Sizes); i++)
					{
						X += Size.cx;

						TextLength = StrFormat(Text, L"%dx%d  ", Sizes[i][0], Sizes[i][1]);
						GetTextExtentPoint32W(Context, Text, TextLength, &Size);

						RECT Rect = { X, Y0 - Size.cy, X + Size.cx, Y0 };
						BOOL Hovering = PtInRect(&Rect, CursorPos);
						SetTextColor(Context, Hovering ? RGB(255, 255, 255) : RGB(192, 192, 192));
						ExtTextOutW(Context, X, Y0, 0, NULL, Text, TextLength, NULL);

						if (Hovering)
						{
							gRectSetSize[0] = Sizes[i][0];
							gRectSetSize[1] = Sizes[i][1];
							SetCursor(gCursorClick);
						}
					}
				}
				else
				{
					// draw initial message when no rectangle is selected
					SelectObject(Context, gFont);
					SelectObject(Context, GetStockObject(DC_PEN));
					SelectObject(Context, GetStockObject(DC_BRUSH));

					const WCHAR Line1[] = L"按住鼠标左键框选录制区域，按回车键开始录制。";
					const WCHAR Line2[] = L"按 ESC 键取消。";

					const WCHAR* Lines[] = { Line1, Line2 };
					const int LineLengths[] = { _countof(Line1) - 1, _countof(Line2) - 1 };
					int Widths[_countof(Lines)];
					int Height;

					int TotalWidth = 0;
					int TotalHeight = 0;
					for (int i = 0; i < _countof(Lines); i++)
					{
						SIZE Size;
						GetTextExtentPoint32W(Context, Lines[i], LineLengths[i], &Size);
						Widths[i] = Size.cx;
						Height = Size.cy;
						TotalWidth = max(TotalWidth, Size.cx);
						TotalHeight += Size.cy;
					}
					TotalWidth += 2 * Height;
					TotalHeight += Height;

					int MsgX = (gRectWidth - TotalWidth) / 2;
					int MsgY = (gRectHeight - TotalHeight) / 2;

					SetDCPenColor(Context, RGB(255, 255, 255));
					SetDCBrushColor(Context, RGB(0, 0, 128));
					Rectangle(Context, MsgX, MsgY, MsgX + TotalWidth, MsgY + TotalHeight);

					SetTextAlign(Context, TA_TOP | TA_CENTER);
					SetTextColor(Context, RGB(255, 255, 0));
					SetBkMode(Context, TRANSPARENT);
					int Y = MsgY + Height / 2;
					int X = gRectWidth / 2;
					for (int i = 0; i < _countof(Lines); i++)
					{
						ExtTextOutW(Context, X, Y, 0, NULL, Lines[i], LineLengths[i], NULL);
						Y += Height;
					}
				}
			}
			else
			{
				RECT Rect;
				GetClientRect(Window, &Rect);

				HBRUSH BorderBrush = CreateSolidBrush(RGB(255, 255, 0));
				Assert(BorderBrush);
				FillRect(Context, &Rect, BorderBrush);
				DeleteObject(BorderBrush);

				Rect.left += WCAP_RECT_BORDER;
				Rect.top += WCAP_RECT_BORDER;
				Rect.right -= WCAP_RECT_BORDER;
				Rect.bottom -= WCAP_RECT_BORDER;

				HBRUSH ColorKeyBrush = CreateSolidBrush(RGB(255, 0, 255));
				Assert(ColorKeyBrush);
				FillRect(Context, &Rect, ColorKeyBrush);
				DeleteObject(ColorKeyBrush);

				FrameRect(Context, &Rect, GetStockObject(BLACK_BRUSH));
			}

			EndBufferedPaint(BufferedPaint, TRUE);
		}

		EndPaint(Window, &Paint);
		return 0;
	}

	return DefWindowProcW(Window, Message, WParam, LParam);
}

static bool OnCaptureFrame(ScreenCapture* Capture, ScreenCaptureFrame* Frame)
{
	if (Frame == NULL)
	{
		PostMessageW(gWindow, WM_WCAP_STOP_CAPTURE, 0, 0);
		return true;
	}

	BOOL DoEncode = TRUE;
	DWORD LimitFramerate = gRecordingLimitFramerate;
	if (LimitFramerate != 0)
	{
		if (Frame->Time * LimitFramerate < gRecordingNextEncode)
		{
			DoEncode = FALSE;
		}
		else
		{
			if (gRecordingNextEncode == 0)
			{
				gRecordingNextEncode = Frame->Time * LimitFramerate;
			}
			gRecordingNextEncode += gTickFreq.QuadPart;
		}
	}

	// ignore frames if it comes from the past
	if (Frame->Time <= gRecordingLastFrame)
	{
		DoEncode = FALSE;
	}
	gRecordingLastFrame = Frame->Time;

	if (DoEncode)
	{
		if (!Encoder_NewFrame(&gEncoder, Frame->Texture, Frame->Rect, Frame->Time, gTickFreq.QuadPart))
		{
			// TODO: maybe highlight tray icon when droppped frames are increasing too much?
			gRecordingDroppedFrames++;
		}
	}

	if (gConfig.EnableLimitLength || gConfig.EnableLimitSize)
	{
		BOOL Stop = FALSE;

		if (gConfig.EnableLimitLength)
		{
			if (Frame->Time - gEncoder.StartTime >= (UINT64)(gConfig.LimitLength * gTickFreq.QuadPart))
			{
				Stop = TRUE;
			}
		}
		if (gConfig.EnableLimitSize && !Stop)
		{
			UINT64 FileSize;
			DWORD Bitrate, LengthMsec;
			Encoder_GetStats(&gEncoder, &Bitrate, &LengthMsec, &FileSize);

			// reserve 0.5% for mp4 format overhead (probably an overestimate)
			if (1000 * FileSize >= (995ULL * gConfig.LimitSize) << 20)
			{
				Stop = TRUE;
			}
		}

		if (Stop)
		{
			PostMessageW(gWindow, WM_WCAP_STOP_CAPTURE, 0, 0);
			return true;
		}
	}

	// update tray title with stats once every second
	if (gRecordingNextTooltip == 0)
	{
		gRecordingNextTooltip = Frame->Time + gTickFreq.QuadPart;
	}
	else if (Frame->Time >= gRecordingNextTooltip)
	{
		gRecordingNextTooltip += gTickFreq.QuadPart;

		// do the update, but not from frame callback to minimize time when texture is used
		PostMessageW(gWindow, WM_WCAP_TRAY_TITLE, 0, 0);
	}

	return true;
}

#ifndef NDEBUG
int WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR cmdline, int cmdshow)
#else
void WinMainCRTStartup()
#endif
{
	WNDCLASSEXW WindowClass =
	{
		.cbSize = sizeof(WindowClass),
		.lpfnWndProc = WindowProc,
		.hInstance = GetModuleHandleW(NULL),
		.lpszClassName = L"wcap_window_class",
	};

	HWND Existing = FindWindowW(WindowClass.lpszClassName, NULL);
	if (Existing)
	{
		PostMessageW(Existing, WM_WCAP_ALREADY_RUNNING, 0, 0);
		ExitProcess(0);
	}

	if (!ScreenCapture_IsSupported())
	{
		MessageBoxW(NULL, L"需要 Windows 10 1903 版本（2019 年 5 月更新，19H1）或更高版本！", WCAP_TITLE, MB_ICONEXCLAMATION);
		ExitProcess(0);
	}

	GetModuleFileNameW(NULL, gConfigPath, _countof(gConfigPath));
	PathRenameExtensionW(gConfigPath, L".ini");

	HR(CoInitializeEx(0, COINIT_APARTMENTTHREADED));

	Config_Defaults(&gConfig);
	Config_Load(&gConfig, gConfigPath);
	ScreenCapture_Create(&gCapture, &OnCaptureFrame, false);
	Encoder_Init(&gEncoder);
	Sound_Init();

	QueryPerformanceFrequency(&gTickFreq);

	gCursorArrow = LoadCursor(NULL, IDC_ARROW);
	gCursorClick = LoadCursor(NULL, IDC_HAND);
	gCursorResize[WCAP_RESIZE_NONE] = LoadCursor(NULL, IDC_CROSS);
	gCursorResize[WCAP_RESIZE_M]    = LoadCursor(NULL, IDC_SIZEALL);
	gCursorResize[WCAP_RESIZE_T]    = gCursorResize[WCAP_RESIZE_B]  = LoadCursor(NULL, IDC_SIZENS);
	gCursorResize[WCAP_RESIZE_L]    = gCursorResize[WCAP_RESIZE_R]  = LoadCursor(NULL, IDC_SIZEWE);
	gCursorResize[WCAP_RESIZE_TL]   = gCursorResize[WCAP_RESIZE_BR] = LoadCursor(NULL, IDC_SIZENWSE);
	gCursorResize[WCAP_RESIZE_TR]   = gCursorResize[WCAP_RESIZE_BL] = LoadCursor(NULL, IDC_SIZENESW);

	gFont = CreateFontW(-WCAP_UI_FONT_SIZE, 0, 0, 0, FW_NORMAL,
		FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, DEFAULT_PITCH, WCAP_UI_FONT);
	Assert(gFont);

	gFontBold = CreateFontW(-WCAP_UI_FONT_SIZE, 0, 0, 0, FW_BOLD,
		FALSE, FALSE, FALSE,DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, DEFAULT_PITCH, WCAP_UI_FONT);
	Assert(gFontBold);

	gIcon1 = LoadIconW(WindowClass.hInstance, MAKEINTRESOURCEW(1));
	gIcon2 = LoadIconW(WindowClass.hInstance, MAKEINTRESOURCEW(2));
	Assert(gIcon1 && gIcon2);

	WM_TASKBARCREATED = RegisterWindowMessageW(L"TaskbarCreated");
	Assert(WM_TASKBARCREATED);

	ATOM Atom = RegisterClassExW(&WindowClass);
	Assert(Atom);

	gWindow = CreateWindowExW(
		0, WindowClass.lpszClassName, WCAP_TITLE, WS_POPUP,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		NULL, NULL, WindowClass.hInstance, NULL);
	if (!gWindow)
	{
		ExitProcess(0);
	}
	if (!EnableHotKeys())
	{
		MessageBoxW(NULL,
			L"无法注册 wcap 全局快捷键。\n可能有其他程序已经占用了相同的组合键。\n请在设置中调整快捷键。",
			WCAP_TITLE, MB_ICONEXCLAMATION);
	}

	for (;;)
	{
		MSG Message;
		BOOL Result = GetMessageW(&Message, NULL, 0, 0);
		if (Result == 0)
		{
			ExitProcess(0);
		}
		Assert(Result > 0);

		TranslateMessage(&Message);
		DispatchMessageW(&Message);
	}
}
