#include "stdafx.h"
#include "resource.h"

#include "SDL_syswm.h"

void resize_statusbar(HWND hStatusBar, WPARAM wParam, LPARAM lParam);
void handle_wm_command(HWND hWnd, WPARAM wParam, LPARAM lParam);
void window_accelerator_message_hook(void *userdata, void *hwnd, unsigned int message, Uint64 wParam, Sint64 lParam);
HWND get_sdl_window_hwnd(SDL_Window *window);

void initialize_ui_from_setting(NisetroPreviewSDLSetting *setting);

static NisetroPreviewSDL *nisetro = NULL;
static HWND hMainWindow;
static HMENU hMenu;
static HACCEL hAccel;

int APIENTRY _tWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR lpCmdLine, int nCmdShow)
{
	char tmp[MAX_PATH];

	INITCOMMONCONTROLSEX iccx;
	iccx.dwSize = sizeof(INITCOMMONCONTROLSEX);
	iccx.dwICC = ICC_BAR_CLASSES;
	if (!InitCommonControlsEx(&iccx))
		return FALSE;

	// load settings	
	NisetroPreviewSDLSetting setting("setting.json");

	if (setting.getDebugConsoleEnabled())
	{
		if (AllocConsole())
		{
			CONSOLE_SCREEN_BUFFER_INFO info;
			HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);

			if (GetConsoleScreenBufferInfo(h, &info))
			{
				info.dwSize.Y = 1024;
				SetConsoleScreenBufferSize(h, info.dwSize);
			}

			freopen("CONOUT$", "w", stdout);
			freopen("CONOUT$", "w", stderr);

			setbuf(stdout, NULL);
			setbuf(stderr, NULL);
		}
	}

	if (setting.getDisableScreenSaver())
		SDL_DisableScreenSaver();
	
	// load window title from resource
	LPWSTR wstr = NULL;
	LoadStringW(hInstance, IDS_APP_TITLE, reinterpret_cast<LPWSTR>(&wstr), 0);
	WideCharToMultiByte(CP_UTF8, 0, wstr, -1, tmp, _countof(tmp), NULL, NULL);

	// register user event
	// SDL_RegisterEvents(1024);

	// create SDL window
	SDL_Window *window = SDL_CreateWindow(tmp,
										  SDL_WINDOWPOS_UNDEFINED,
										  SDL_WINDOWPOS_UNDEFINED,
										  1,
										  1,
										  SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);

	if (window == NULL)
	{
		SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "Unable to create window: %s", SDL_GetError());
		return -1;
	}

	nisetro = new NisetroPreviewSDL(window, &setting);
	
	if (!nisetro->init())
		goto at_nisetro_exit;

	hMainWindow = get_sdl_window_hwnd(window);
	hMenu = LoadMenu(hInstance, MAKEINTRESOURCE(ID_MENU_NISETROPREVIEWSDL));
	hAccel = LoadAccelerators(hInstance, MAKEINTRESOURCE(ID_ACCEL_NISETROPREVIEWSDL));

	SetMenu(hMainWindow, hMenu);
	//HWND hStatusBar = CreateWindowEx(0, STATUSCLASSNAME, NULL, SBARS_SIZEGRIP | WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hWnd, (HMENU)ID_STATUSBAR, hInstance, NULL);
	//SendMessage(hStatusBar, WM_SIZE, 0, 0);

	SDL_SetWindowsMessageHook(window_accelerator_message_hook, NULL);
	SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);

	// initialize UI state
	initialize_ui_from_setting(&setting);

	// start message loop
	SDL_Event sdl_event;

	do
	{
		if (SDL_WaitEvent(&sdl_event))
		{
			if (sdl_event.type == SDL_SYSWMEVENT)
			{
				auto win_msg = sdl_event.syswm.msg->msg.win;

				switch (win_msg.msg)
				{
					case WM_COMMAND:
					{
						handle_wm_command(win_msg.hwnd, win_msg.wParam, win_msg.lParam);
						break;
					}
					case WM_DEVICECHANGE:
					{
						// device state changed
						// TODO usb id from setting
						nisetro->handleDeviceChangeEvent(win_msg.hwnd, win_msg.wParam, win_msg.lParam, 0);
						break;
					}
				}
			}
			else if (sdl_event.type == SDL_WINDOWEVENT)
			{
				nisetro->handleWindowEvent(&sdl_event.window);
			}
			else if (sdl_event.type == SDL_USEREVENT)
			{
				nisetro->handleUserEvent(&sdl_event.user);
			}
		}

	} while (sdl_event.type != SDL_QUIT);

	SDL_SetWindowsMessageHook(NULL, NULL);

at_nisetro_exit:
	delete nisetro;
	SDL_Quit();

	FreeConsole();

	return 0;
}

void handle_wm_command(HWND hwnd, WPARAM wParam, LPARAM lParam)
{
	static int wmId, wmEvent;
	static SDL_Event sdl_event;
	static UINT menu_state;

	wmId = LOWORD(wParam);
	wmEvent = HIWORD(wParam);

	switch (wmId)
	{
		case ID_MENU_FILE_SCREENSHOT:
		{
			nisetro->requestScreenshot();
			break;
		}
		case ID_MENU_FILE_EXIT:
		{
			SDL_memset(&sdl_event, 0, sizeof(sdl_event));
			sdl_event.type = SDL_QUIT;
			SDL_PushEvent(&sdl_event);
			break;
		}
		case ID_MENU_OPTIONS_VIDEO_SIZE_X1:
		case ID_MENU_OPTIONS_VIDEO_SIZE_X2:
		case ID_MENU_OPTIONS_VIDEO_SIZE_X3:
		case ID_MENU_OPTIONS_VIDEO_SIZE_X4:
		case ID_MENU_OPTIONS_VIDEO_SIZE_X5:
		{
			if (CheckMenuRadioItem(hMenu, ID_MENU_OPTIONS_VIDEO_SIZE_X1, ID_MENU_OPTIONS_VIDEO_SIZE_X5, wmId, MF_BYCOMMAND))
				nisetro->setWindowSize(wmId - ID_MENU_OPTIONS_VIDEO_SIZE_X1 + 1);

			break;
		}
		case ID_MENU_OPTIONS_VIDEO_ROTATE_AUTO:
		case ID_MENU_OPTIONS_VIDEO_ROTATE_VERTICAL:
		case ID_MENU_OPTIONS_VIDEO_ROTATE_HORIZONTAL:
		case ID_MENU_OPTIONS_VIDEO_ROTATE_KERORICAN:
		{
			if (CheckMenuRadioItem(hMenu, ID_MENU_OPTIONS_VIDEO_ROTATE_AUTO, ID_MENU_OPTIONS_VIDEO_ROTATE_KERORICAN, wmId, MF_BYCOMMAND))
				nisetro->setWindowRotateMethod(wmId - ID_MENU_OPTIONS_VIDEO_ROTATE_AUTO);

			break;
		}
		case ID_MENU_OPTIONS_AUDIO_ENABLEINTERPOLATION:
		{
			menu_state = GetMenuState(hMenu, ID_MENU_OPTIONS_AUDIO_ENABLEINTERPOLATION, MF_BYCOMMAND);
			menu_state = ((menu_state & MF_CHECKED) ? MF_UNCHECKED : MF_CHECKED);
			CheckMenuItem(hMenu, ID_MENU_OPTIONS_AUDIO_ENABLEINTERPOLATION, menu_state);
			
			nisetro->setAudioEnableInterpolation((menu_state ? 1 : 0));

			break;
		}
		case ID_MENU_OPTIONS_AUDIO_MUTEAUDIO:
		{
			menu_state = GetMenuState(hMenu, ID_MENU_OPTIONS_AUDIO_MUTEAUDIO, MF_BYCOMMAND);
			menu_state = ((menu_state & MF_CHECKED) ? MF_UNCHECKED : MF_CHECKED);
			CheckMenuItem(hMenu, ID_MENU_OPTIONS_AUDIO_MUTEAUDIO, menu_state);

			nisetro->setAudioMute((menu_state ? true : false));
			break;
		}
		case ID_MENU_OPTIONS_USB_DEVICEID_0:
		case ID_MENU_OPTIONS_USB_DEVICEID_1:
		case ID_MENU_OPTIONS_USB_DEVICEID_2:
		case ID_MENU_OPTIONS_USB_DEVICEID_3:
		case ID_MENU_OPTIONS_USB_DEVICEID_4:
		case ID_MENU_OPTIONS_USB_DEVICEID_5:
		case ID_MENU_OPTIONS_USB_DEVICEID_6:
		case ID_MENU_OPTIONS_USB_DEVICEID_7:
		case ID_MENU_OPTIONS_USB_DEVICEID_8:
		case ID_MENU_OPTIONS_USB_DEVICEID_9:
		{
			if (CheckMenuRadioItem(hMenu, ID_MENU_OPTIONS_USB_DEVICEID_0, ID_MENU_OPTIONS_USB_DEVICEID_9, wmId, MF_BYCOMMAND))
			{
				SDL_memset(&sdl_event, 0, sizeof(sdl_event));

				sdl_event.type = SDL_USEREVENT;
				sdl_event.user.code = 1023;
				sdl_event.user.data1 = reinterpret_cast<void*>(wmId - ID_MENU_OPTIONS_USB_DEVICEID_0);
				sdl_event.user.data2 = hMainWindow;

				SDL_PushEvent(&sdl_event);
			}

			break;
		}
		case ID_MENU_HELP_ABOUT:
		{
			// TODO
			break;
		}
	}
}

void resize_statusbar(HWND hStatusBar, WPARAM wParam, LPARAM lParam)
{
	int part_width[2] = { LOWORD(lParam) - 100, -1 };

	SendMessage(hStatusBar, SB_SETPARTS, (WPARAM)2, (LPARAM)part_width);
	SendMessage(hStatusBar, WM_SIZE, wParam, lParam);
}

void initialize_ui_from_setting(NisetroPreviewSDLSetting *setting)
{
	// TODO options from setting
	CheckMenuRadioItem(hMenu, ID_MENU_OPTIONS_VIDEO_ROTATE_AUTO, ID_MENU_OPTIONS_VIDEO_ROTATE_HORIZONTAL, ID_MENU_OPTIONS_VIDEO_ROTATE_AUTO, MF_BYCOMMAND);
	CheckMenuRadioItem(hMenu, ID_MENU_OPTIONS_VIDEO_SIZE_X1, ID_MENU_OPTIONS_VIDEO_SIZE_X5, ID_MENU_OPTIONS_VIDEO_SIZE_X1, MF_BYCOMMAND);

	CheckMenuItem(hMenu, ID_MENU_OPTIONS_AUDIO_MUTEAUDIO, (setting->getAudioVolume() < 0 ? MF_CHECKED : MF_UNCHECKED));
	CheckMenuItem(hMenu, ID_MENU_OPTIONS_AUDIO_ENABLEINTERPOLATION, (setting->getAudioInterpolationEnabled() ? MF_CHECKED : MF_UNCHECKED));

	// TODO should apply inside constructor
	nisetro->setWindowRotateMethod(0);
	nisetro->setWindowSize(1);

	nisetro->setAudioEnableInterpolation((setting->getAudioInterpolationEnabled() ? 1 : 0));
	nisetro->setAudioVolume(setting->getAudioVolume());
	nisetro->reopenAudioDevice(setting->getAudioDeviceName());

	// Finally, setup USB
	// TODO usb id from setting
	PostMessage(hMainWindow, WM_COMMAND, MAKEWPARAM(ID_MENU_OPTIONS_USB_DEVICEID_0, 0), MAKELPARAM(0, 0));
}

void window_accelerator_message_hook(void *userdata, void *hwnd, unsigned int message, Uint64 wParam, Sint64 lParam)
{
	static MSG msg;

	msg.hwnd = reinterpret_cast<HWND>(hwnd);
	msg.message = message;
	msg.wParam = (WPARAM)wParam;
	msg.lParam = (LPARAM)lParam;

	TranslateAccelerator(hMainWindow, hAccel, &msg);
}

HWND get_sdl_window_hwnd(SDL_Window *window)
{
	SDL_SysWMinfo wm_info;
	SDL_VERSION(&wm_info.version);

	if (SDL_GetWindowWMInfo(window, &wm_info) == SDL_FALSE)
	{
		SDL_LogError(SDL_LOG_CATEGORY_SYSTEM, "Unable to get window wm info: %s", SDL_GetError());
		return NULL;
	}

	return wm_info.info.win.window;
}
