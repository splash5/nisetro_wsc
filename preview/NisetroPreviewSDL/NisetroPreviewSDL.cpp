#include "NisetroPreviewSDL.h"
#include "samplerate.h"

#ifdef  _POSIX_
#include <sys/time.h>
#else
#if defined(_MSC_VER) || defined(_MSC_EXTENSIONS)
#define DELTA_EPOCH_IN_MICROSECS  116444736000000000Ui64
#else
#define DELTA_EPOCH_IN_MICROSECS  116444736000000000ULL
#endif

#define FILETIME_UNITS_PER_SEC	10000000L
#define FILETIME_UNITS_PER_USEC 10
#endif

#include <stdio.h>
#include <time.h>

#define AUDIO_DEVICE_BUFFER_SAMPLES		32
#define AUDIO_PROCESS_BUFFER_SIZE		1024

const uint64_t NisetroPreviewSDL::performanceFrequency = SDL_GetPerformanceFrequency();
const SDL_Rect NisetroPreviewSDL::frameSurfaceRect = { 0, 0, VIDEO_FRAME_LOGICAL_WIDTH, VIDEO_FRAME_LOGICAL_HEIGHT };
const SDL_Rect NisetroPreviewSDL::renderSurfaceRect = { 0, 0, VIDEO_FRAME_VALID_WIDTH, VIDEO_FRAME_VALID_LINES };
const SDL_Rect NisetroPreviewSDL::segment_mask_rects[] =
{
	{ 0, 0, 0, 0 },	// X
	{ 0, 0, 0, 0 },	// X
	{ 0, 926, 56, 44 },	// POWER
	{ 0, 851, 56, 33 },	// CARTRIDGE
	{ 0, 736, 56, 61 },	// SLEEP
	{ 0, 585, 56, 96 },	// LOW_BATTERY

	{ 0, 496, 56, 36 },	// VOLUME_0
	{ 0, 465, 56, 13 },	// VOLUME_2
	{ 0, 446, 56, 17 },	// VOLUME_3

	{ 0, 360, 56, 45 },	// HEADPHONE
	{ 0, 287, 56, 43 },	// VERTICAL
	{ 0, 206, 56, 44 },	// HORIZONTAL
	{ 0, 153, 56, 19 },	// DOT1
	{ 0, 105, 56, 24 },	// DOT2
	{ 0, 38, 56, 40 },	// DOT3

	{ 0, 482, 56, 8 }	// VOLUME_1
};

NisetroPreviewSDLSetting NisetroPreviewSDL::defaultSetting;

NisetroPreviewSDL::NisetroPreviewSDL(SDL_Window *window, NisetroPreviewSDLSetting *setting)
{
	SDL_LogSetPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO);
	SDL_LogSetPriority(SDL_LOG_CATEGORY_VIDEO, SDL_LOG_PRIORITY_INFO);
	SDL_LogSetPriority(SDL_LOG_CATEGORY_RENDER, SDL_LOG_PRIORITY_INFO);
	SDL_LogSetPriority(SDL_LOG_CATEGORY_AUDIO, SDL_LOG_PRIORITY_INFO);

	SDL_LogSetPriority(SDL_LOG_CATEGORY_ASSERT, SDL_LOG_PRIORITY_CRITICAL);
	SDL_LogSetPriority(SDL_LOG_CATEGORY_SYSTEM, SDL_LOG_PRIORITY_DEBUG);
	SDL_LogSetPriority(SDL_LOG_CATEGORY_ERROR, SDL_LOG_PRIORITY_ERROR);

	SDL_version sdl_version;
	SDL_GetVersion(&sdl_version);
	SDL_LogInfo(SDL_LOG_CATEGORY_SYSTEM, "SDL Version: %d.%d.%d", sdl_version.major, sdl_version.minor, sdl_version.patch);

	window_ = window;
	setting_ = setting;

	if (setting_ == NULL)
		setting_ = &NisetroPreviewSDL::defaultSetting;

	usb_device_ = NULL;
	usb_out_ep_ = NULL;
	usb_device_id_ = 10;

	video_frame_surface_ = NULL;
	render_surface_ = NULL;
	segment_bg_surface_ = NULL;
	segment_icon_surface_ = NULL;
	
	audio_device_id_ = 0;
	audio_out_stream_ = NULL;

	render_params_lock_ = 0;
	audioplay_params_lock_ = 0;
	render_params_dirty_ = false;

	window_width_ = -1;
	window_height_ = -1;
	window_rotate_method_ = -1;
	window_orientation_ = -1;

	frame_copy_frect_.x = 0.0f;
	frame_copy_frect_.y = 0.0f;
	frame_copy_frect_.w = VIDEO_FRAME_VALID_WIDTH;
	frame_copy_frect_.h = VIDEO_FRAME_VALID_LINES;

	segment_copy_frect_.x = frame_copy_frect_.w;
	segment_copy_frect_.y = 0.0f;
	segment_copy_frect_.w = VIDEO_FRAME_SEGMENT_WIDTH;
	segment_copy_frect_.h = VIDEO_FRAME_VALID_LINES;

	render_rotate_angle_ = 0.0;
	
	audio_enable_interpolation_ = -1;
	audio_volume_ = -SDL_MIX_MAXVOLUME - 1;
	audio_device_name_ = NULL;

	// default is horizonal
	screen_orientation_ = 2;

	ringbuffer_init(&capture_ringbuffer_, capture_buffer_, sizeof(uint16_t), sizeof(capture_buffer_) / sizeof(uint16_t));
	ringbuffer_init(&audio_ringbuffer_, audio_buffer_, sizeof(uint32_t), 1024);
}

NisetroPreviewSDL::~NisetroPreviewSDL(void)
{
	closeUSB();

	if (video_frame_surface_)
		SDL_FreeSurface(video_frame_surface_);

	if (render_surface_)
		SDL_FreeSurface(render_surface_);

	if (segment_bg_surface_)
	{
		SDL_free(segment_bg_surface_->userdata);
		SDL_FreeSurface(segment_bg_surface_);
	}

	if (segment_icon_surface_)
		SDL_FreeSurface(segment_icon_surface_);

	if (audio_device_id_ > 0)
	{
		SDL_PauseAudioDevice(audio_device_id_, 1);
		SDL_CloseAudioDevice(audio_device_id_);
	}

	if (audio_out_stream_)
	{
		SDL_AudioStreamClear(audio_out_stream_);
		SDL_FreeAudioStream(audio_out_stream_);
	}

	SDL_free(audio_device_name_);
}

bool NisetroPreviewSDL::init(void)
{
	if (!SDL_WasInit(SDL_INIT_VIDEO))
	{
		if (SDL_InitSubSystem(SDL_INIT_VIDEO))
		{
			SDL_LogError(SDL_LOG_CATEGORY_ASSERT, "Unable to init SDL: %s", SDL_GetError());
			return false;
		}
	}

	if (!SDL_WasInit(SDL_INIT_AUDIO))
	{
		if (SDL_InitSubSystem(SDL_INIT_AUDIO))
		{
			SDL_LogError(SDL_LOG_CATEGORY_ASSERT, "Unable to init SDL: %s", SDL_GetError());
			return false;
		}
	}

	// for filling raw RGB444 pixel data
	SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, VIDEO_FRAME_WIDTH, VIDEO_FRAME_VALID_LINES, 16, SDL_PIXELFORMAT_XRGB4444);

	if (surface == NULL)
	{
		SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "Unable to create video frame surface: %s", SDL_GetError());
		return false;
	}

	video_frame_surface_ = surface;
	
	// for rendering
	render_surface_ = SDL_CreateRGBSurfaceWithFormat(0, VIDEO_FRAME_WIDTH, VIDEO_FRAME_VALID_LINES, 16, SDL_PIXELFORMAT_XRGB4444);

	// for segment drawing
	// load from bmp
	SDL_Rect *rect = SDL_reinterpret_cast(SDL_Rect*, SDL_calloc(1, sizeof(SDL_Rect)));
	segment_bg_surface_ = SDL_LoadBMP_RW(SDL_RWFromConstMem(SDL_reinterpret_cast(const void*, NisetroPreviewSDL::segment_bg_bitmap), sizeof(NisetroPreviewSDL::segment_bg_bitmap)), SDL_TRUE);
	segment_bg_surface_->userdata = rect;
	rect->w = segment_bg_surface_->w;
	rect->h = segment_bg_surface_->h;

	segment_icon_surface_ = SDL_LoadBMP_RW(SDL_RWFromConstMem(SDL_reinterpret_cast(const void*, NisetroPreviewSDL::segment_icon_bitmap), sizeof(NisetroPreviewSDL::segment_icon_bitmap)), SDL_TRUE);

	SDL_SetSurfaceBlendMode(video_frame_surface_, SDL_BLENDMODE_NONE);
	SDL_SetSurfaceBlendMode(render_surface_, SDL_BLENDMODE_NONE);
	SDL_SetSurfaceBlendMode(segment_bg_surface_, SDL_BLENDMODE_NONE);

	return true;
}

void NisetroPreviewSDL::handleDeviceChangeEvent(HWND hWnd, WPARAM wParam, LPARAM lParam, uint8_t usb_device_id)
{
	// cfx2log exists, maybe receiving:
	// 1. remove
	// 2. device node change
	// we need to close current usb device then trying to reopen it
	// to see if it can be connect again

	if (isCFX2LogExists())
	{
		if (usb_device_->PnpEvent(wParam, lParam))
			resetUSB(usb_device_id, hWnd);
	}
	else
	{
		resetUSB(usb_device_id, hWnd);
	}
}

void NisetroPreviewSDL::handleWindowEvent(const SDL_WindowEvent *window_event)
{
	switch (window_event->event)
	{
		case SDL_WINDOWEVENT_SIZE_CHANGED:
		{
			window_width_  = window_event->data1;
			window_height_ = window_event->data2;

			SDL_AtomicLock(&render_params_lock_);
			{
				switch (window_orientation_)
				{
					case 1:	// vertical
					{
						frame_copy_frect_.y = VIDEO_FRAME_SEGMENT_WIDTH + (VIDEO_FRAME_VALID_WIDTH - VIDEO_FRAME_VALID_LINES) / 2.0f;
						frame_copy_frect_.x = -(VIDEO_FRAME_VALID_WIDTH - VIDEO_FRAME_VALID_LINES) / 2.0f;
						segment_copy_frect_.x = (VIDEO_FRAME_VALID_LINES - VIDEO_FRAME_SEGMENT_WIDTH) / 2.0f;
						segment_copy_frect_.y = -segment_copy_frect_.x;
						render_rotate_angle_ = -90.0;
						break;
					}
					case 2:	// horizontal
					{
						frame_copy_frect_.x = 0.0f;
						frame_copy_frect_.y = 0.0f;
						segment_copy_frect_.x = VIDEO_FRAME_VALID_WIDTH;
						segment_copy_frect_.y = 0.0f;
						render_rotate_angle_ = 0.0;
						break;
					}
					case 3:	// kerorican
					{
 						frame_copy_frect_.x = (float)(VIDEO_FRAME_VALID_LINES * KERORICAN_ROTATE_SINE - VIDEO_FRAME_VALID_WIDTH * (1.0 - KERORICAN_ROTATE_COSINE)) / 2.0;
						frame_copy_frect_.y = (float)(((VIDEO_FRAME_VALID_WIDTH + VIDEO_FRAME_SEGMENT_WIDTH * 2) * KERORICAN_ROTATE_SINE) - VIDEO_FRAME_VALID_LINES * (1.0 - KERORICAN_ROTATE_COSINE)) / 2.0;
						segment_copy_frect_.x = (float)(VIDEO_FRAME_VALID_LINES * KERORICAN_ROTATE_SINE + (VIDEO_FRAME_VALID_WIDTH * 2 + VIDEO_FRAME_SEGMENT_WIDTH) * KERORICAN_ROTATE_COSINE - VIDEO_FRAME_SEGMENT_WIDTH) / 2.0;
						segment_copy_frect_.y = (float)((VIDEO_FRAME_SEGMENT_WIDTH * KERORICAN_ROTATE_SINE) - VIDEO_FRAME_VALID_LINES * (1.0 - KERORICAN_ROTATE_COSINE)) / 2.0;
						render_rotate_angle_ = -KERORICAN_ROTATE_DEGREE;
						break;
					}
				}

				render_params_dirty_ = true;
			}
			SDL_AtomicUnlock(&render_params_lock_);

			SDL_LogInfo(SDL_LOG_CATEGORY_SYSTEM, "window size changed %d x %d", window_width_, window_height_);

			break;
		}
		case SDL_WINDOWEVENT_MAXIMIZED:
		{
			window_maximized_ = true;
			SDL_LogInfo(SDL_LOG_CATEGORY_SYSTEM, "window maximized");
			break;
		}
		case SDL_WINDOWEVENT_MINIMIZED:
		{
			SDL_LogInfo(SDL_LOG_CATEGORY_SYSTEM, "window minimized");
			break;
		}
		case SDL_WINDOWEVENT_RESTORED:
		{
			window_maximized_ = false;
			SDL_LogInfo(SDL_LOG_CATEGORY_SYSTEM, "window restored");
			break;
		}
	}
}

void NisetroPreviewSDL::handleUserEvent(const SDL_UserEvent *user_event)
{
	switch (user_event->code)
	{
		case 512:
		{
			if (window_rotate_method_ == 0)
				setWindowOrientation(screen_orientation_);

			break;
		}
		case 513:
		{
			// screenshot
			SDL_Surface *surface = reinterpret_cast<SDL_Surface*>(user_event->data1);
			int32_t orientation = reinterpret_cast<int32_t>(user_event->data2);

			// cut out non-visible area
			// TODO use a flag to cut out lcd segment area
			surface->w = 224;		

			char screenshot_filepath[4096];
			getLocalTimeString(screenshot_filepath + sizeof(screenshot_filepath) - 18, 18);
			SDL_snprintf(screenshot_filepath, sizeof(screenshot_filepath), "%s%s.bmp", setting_->getScreenshotPath(), screenshot_filepath + sizeof(screenshot_filepath) - 18);

			if (SDL_SaveBMP(surface, screenshot_filepath) == 0)
				SDL_LogInfo(SDL_LOG_CATEGORY_SYSTEM, "Save screenshot to %s successfully.", screenshot_filepath);

			SDL_FreeSurface(surface);
			break;
		}
		case 1023:
		{
			uint8_t usb_device_id = (uint8_t)user_event->data1;

			resetUSB(usb_device_id, user_event->data2);
			setting_->setUSBDeviceID(usb_device_id);

			break;
		}

		// TODO
		default: break;
	}
}

void NisetroPreviewSDL::requestScreenshot(void)
{
	SDL_AtomicSet(&request_screenshot_, SDL_TRUE);
}

void NisetroPreviewSDL::closeUSB(void)
{
	if (!isCFX2LogExists())
		return;

	// stop usb thread
	cusb_tcb_->looping = false;
	audio_tcb_->looping = false;
	usb_device_->delete_thread(cusb_tcb_);
	usb_device_->delete_thread(audio_tcb_);

	keep_running_ = false;

	if (video_thread_)
	{
		SDL_LockMutex(video_thread_mutex_);
		SDL_CondSignal(video_thread_cond_);
		SDL_UnlockMutex(video_thread_mutex_);

		SDL_WaitThread(video_thread_, NULL);
		SDL_DestroyMutex(video_thread_mutex_);
		SDL_DestroyCond(video_thread_cond_);
	}

	if (audio_thread_)
	{
		SDL_LockMutex(audio_thread_mutex_);
		SDL_CondSignal(audio_thread_cond_);
		SDL_UnlockMutex(audio_thread_mutex_);

		SDL_WaitThread(audio_thread_, NULL);
		SDL_DestroyMutex(audio_thread_mutex_);
		SDL_DestroyCond(audio_thread_cond_);
	}

	if (render_thread_)
	{
		SDL_LockMutex(render_thread_mutex_);
		SDL_CondSignal(render_thread_cond_);
		SDL_UnlockMutex(render_thread_mutex_);

		SDL_WaitThread(render_thread_, NULL);
		SDL_DestroyMutex(render_thread_mutex_);
		SDL_DestroyCond(render_thread_cond_);
	}

	video_thread_ = NULL;
	video_thread_mutex_ = NULL;
	video_thread_cond_ = NULL;

	render_thread_ = NULL;
	render_thread_mutex_ = NULL;
	render_thread_cond_ = NULL;

	audio_thread_ = NULL;
	audio_thread_mutex_ = NULL;
	audio_thread_cond_ = NULL;

	LONG cmd_len = 0;
	uint8_t usb_cmd_buf[USB_CFG_BUF_SIZE];

	usb_cmd_buf[cmd_len++] = CMD_PORT_WRITE;
	usb_cmd_buf[cmd_len++] = PIO_RESET;
	usb_cmd_buf[cmd_len++] = CMD_EP6IN_STOP;
	usb_cmd_buf[cmd_len++] = CMD_EP2IN_STOP;
	usb_cmd_buf[cmd_len++] = CMD_MODE_IDLE;
	usb_device_->xfer(usb_out_ep_, usb_cmd_buf, cmd_len);

	delete usb_device_;
	usb_device_ = NULL;
	usb_out_ep_ = NULL;

	SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "cfx2log2 device closed");
}

bool NisetroPreviewSDL::resetUSB(uint8_t usb_device_id, void *userdata)
{
	HANDLE h = reinterpret_cast<HANDLE>(userdata);

	if (h == NULL)
	{
		SDL_LogError(SDL_LOG_CATEGORY_SYSTEM, "window handle is NULL");
		return false;
	}

	if (usb_device_id > 9)
	{
		SDL_LogError(SDL_LOG_CATEGORY_SYSTEM, "USB device id should only be 0 to 9, but value is %u", usb_device_id);
		return false;
	}

	closeUSB();

	cusb2 *usb = new cusb2(h);

	if (!usb->fwload(usb_device_id, fx2lp_fw_, "CFX2LOG2"))
	{
		SDL_LogError(SDL_LOG_CATEGORY_SYSTEM, "unable to connect to a cfx2log2 device #%d", usb_device_id);
		goto reset_usb_error;
	}

	// get ep1 in and out endpoint
	CCyUSBEndPoint *out_ep = usb->get_endpoint(0x01);
	CCyUSBEndPoint *in_ep = usb->get_endpoint(0x81);

	if (out_ep == NULL || in_ep == NULL)
	{
		SDL_LogError(SDL_LOG_CATEGORY_SYSTEM, "unable to get endpoint from device");
		goto reset_usb_error;
	}

	uint8_t usb_cmd_buf[USB_CFG_BUF_SIZE];
	LONG cmd_len = 0;

	// set PORTD
	usb_cmd_buf[cmd_len++] = CMD_PORT_CFG;
	// PORTD bit[2:0] as max2 register addr
	usb_cmd_buf[cmd_len++] = 0x07;
	// PORTD bit[5:3] as output pin
	usb_cmd_buf[cmd_len++] = PIO_DROP | PIO_RESET | PIO_FIFO_DIR;
	usb_cmd_buf[cmd_len++] = CMD_MODE_IDLE;

	// IFCONFIG will be set when FX2LP received CMD_EP6IN_START or CMD_EP2IN_START
	usb_cmd_buf[cmd_len++] = CMD_IFCONFIG;
	usb_cmd_buf[cmd_len++] = 0xe3;	// slave fifo sync mode, output IFCLK = 48MHz
	usb->xfer(out_ep, usb_cmd_buf, cmd_len);

	// setting up thread
	render_thread_mutex_ = SDL_CreateMutex();
	video_thread_mutex_ = SDL_CreateMutex();
	audio_thread_mutex_ = SDL_CreateMutex();

	render_thread_cond_ = SDL_CreateCond();
	video_thread_cond_ = SDL_CreateCond();
	audio_thread_cond_ = SDL_CreateCond();

	// clean up
	ringbuffer_clear(&capture_ringbuffer_);
	ringbuffer_clear(&audio_ringbuffer_);

	// start processing thread
	keep_running_ = true;
	render_thread_ = SDL_CreateThread(NisetroPreviewSDL::renderThreadProc, "RenderThread", this);
	video_thread_ = SDL_CreateThread(NisetroPreviewSDL::videoThreadProc, "VideoThread", this);
	audio_thread_ = SDL_CreateThread(NisetroPreviewSDL::audioThreadProc, "AudioThread", this);

	usb_device_ = usb;
	usb_out_ep_ = out_ep;
	usb_device_id_ = usb_device_id;

	SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "cfx2log2 device connected, id: %u", usb_device_id_);

	cusb_tcb_ = usb_device_->prepare_thread(VIDEO_EP_ADDR, BUF_LEN, QUE_NUM, NisetroPreviewSDL::cusbThreadProc);
	cusb_tcb_->userpointer = this;

	audio_tcb_ = usb_device_->prepare_thread(AUDIO_EP_ADDR, setting_->getAudioBufferSize(), setting_->getAudioBufferCount(), NisetroPreviewSDL::cusbAudioThreadProc);
	audio_tcb_->userpointer = this;

	usb_device_->start_thread(audio_tcb_);
	usb_device_->start_thread(cusb_tcb_);

	cmd_len = 0;
	usb_cmd_buf[cmd_len++] = CMD_PORT_WRITE;
	usb_cmd_buf[cmd_len++] = PIO_RESET;	// RESET = 1, DIR = FX2toPC
	usb_cmd_buf[cmd_len++] = CMD_EP2IN_START;
	usb_cmd_buf[cmd_len++] = CMD_EP6IN_START;
	usb_cmd_buf[cmd_len++] = CMD_PORT_WRITE;
	usb_cmd_buf[cmd_len++] = 0x00;		// RESET = 0, CPLD start capturing data

	usb_device_->xfer(usb_out_ep_, usb_cmd_buf, cmd_len);

	return true;
reset_usb_error:
	delete usb;
	return false;
}

bool NisetroPreviewSDL::isCFX2LogExists(void)
{
	return (usb_device_ && usb_out_ep_ && usb_device_id_ < 10);
}

void NisetroPreviewSDL::processCaptureData(uint16_t *data, uint32_t element_count)
{	
	uint16_t sync_state;
	uint16_t *pd;
	uint32_t p;

	for (uint32_t i = 0; i < element_count; i++)
	{
		sync_state = (data[i] & 0xcfff);

		// video sync state
		// check if end of hsync
		if ((last_sync_state_ & 0x4fff) == 0x4000 && (sync_state & 0x4fff) == 0x0000)
		{
			// found end of hsync, now checks if end of vsync
			if ((last_sync_state_ & 0x8000) == 0x8000 && (sync_state & 0x8000) == 0x0000)
			{
				// found end of vsync
				video_line_pixels_ = 0;
				video_frame_lines_ = 0;
				video_frame_pixels_ = 0;
				video_frame_count_++;
				line_pixels_ = reinterpret_cast<uint8_t*>(video_frame_surface_->pixels);
				
				frame_synced_ = true;
			}
			else
			{
				// handle hsync (end of line)
				if (frame_synced_ && video_frame_lines_ > 3)
				{
					if (video_frame_lines_ == 4)
					{
						// update LCD segment
						// when in sleep mode, only line #4 contains segment data
						uint16_t lcd_segment_state;						
						pd = pixels_ + 224 + 16;
						p = 0;

						do { lcd_segment_state = ((lcd_segment_state << 1) | (*(--pd) & 0x0001)); } while (++p < 16);

						// enable and power is always on
						if ((lcd_segment_state & (LCD_SEGMENT_POWER | LCD_SEGMENT_ENABLED)) == (LCD_SEGMENT_POWER | LCD_SEGMENT_ENABLED))
						{
							// only update if segment state changed
							if ((lcd_segment_state ^ lcd_segment_state_))
							{
								// see if we need to rotate 
								p = screen_orientation_;

								if ((lcd_segment_state & LCD_SEGMENT_VERTICAL))
									screen_orientation_ = 1;
								else if ((lcd_segment_state & LCD_SEGMENT_HORIZONTAL))
									screen_orientation_ = 2;

								// screen orientation changed
								if (p != screen_orientation_)
								{
									SDL_Event e;
									SDL_memset(&e, 0, sizeof(e));

									e.type = SDL_USEREVENT;
									e.user.code = 512;
									e.user.data1 = reinterpret_cast<void*>(screen_orientation_);

									SDL_PushEvent(&e);
								}
							}

							lcd_segment_state_ = lcd_segment_state;
						}

						// copy segment state to render_surface for later rendering
						// *(pixels_ + VIDEO_FRAME_VALID_WIDTH) = lcd_segment_state_;
					}
					else if (video_frame_lines_ == 147)
					{
						// copies frame surface to render surface so we can keep capturing running.
						// notify render thread to render
						if (video_frame_pixels_ == (VIDEO_FRAME_WIDTH * (VIDEO_FRAME_VALID_LINES + 4)) /* TODO show error frame? */)
						{
							SDL_LockMutex(render_thread_mutex_);
							
							SDL_BlitSurface(video_frame_surface_, NULL, render_surface_, NULL);
							*(SDL_reinterpret_cast(uint16_t*, render_surface_->pixels) + VIDEO_FRAME_VALID_WIDTH) = lcd_segment_state_;
							
							SDL_CondSignal(render_thread_cond_);
							SDL_UnlockMutex(render_thread_mutex_);
						}
						else
						{
							SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO, "AError frame %u", video_frame_count_);
						}
					}

					// move to next line
					line_pixels_ += video_frame_surface_->pitch;
				}

				video_line_pixels_ = 0;
				video_frame_lines_++;
			}

			pixels_ = reinterpret_cast<uint16_t*>(line_pixels_);
		}

		// store sync state
		last_sync_state_ = sync_state;

		if (frame_synced_)
		{
			// check if error frame
			if (video_line_pixels_ > VIDEO_FRAME_WIDTH ||
				video_frame_lines_ >= VIDEO_FRAME_LINES ||
				video_frame_pixels_ > VIDEO_FRAME_PIXELS)
			{
				// error frame
				SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO, "BError frame %u", video_frame_count_);
				frame_synced_ = false;
				continue;
			}

			// collect pixel data
			if (video_frame_lines_ > 3 && video_frame_lines_ < 148 &&
				video_line_pixels_ > 5 && video_line_pixels_ < 246)
			{
				p = (video_line_pixels_ - 6) / 3;
				pd = pixels_ + p;

				*pd = ((*pd << 4) | sync_state & 0x000f); sync_state >>= 4; pd += 80;	// left 80 pixels
				*pd = ((*pd << 4) | sync_state & 0x000f); sync_state >>= 4; pd += 80;	// middle 80 pixels
				*pd = ((*pd << 4) | sync_state & 0x000f); sync_state >>= 4;				// right 64 pixels + 16pixels LCD segment state
			}
						
			video_line_pixels_++;
			video_frame_pixels_++;
		}
	}
}

int NisetroPreviewSDL::renderThreadProc(void *userdata)
{
	SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "render thread started");

	NisetroPreviewSDL *nisetro = reinterpret_cast<NisetroPreviewSDL*>(userdata);
	nisetro->render_params_dirty_ = true;
	
	SDL_Rect frame_rect;
	SDL_FRect frame_copy_frect;
	SDL_FRect segment_copy_frect;
	double render_rotate_angle;
	int backbuffer_size = 0;
	int texture_scale_mode = -1;
	int render_scale_quality = -1;
	int render_wait_timeout;

	uint16_t segment_state = 0, last_segment_state = 0;
	int32_t segment_mask_count;
	SDL_Rect segment_mask[14] = { 0 };

	frame_rect.x = frame_rect.y = 0;

	// create renderer and texture
	SDL_Renderer *renderer = SDL_CreateRenderer(nisetro->window_, -1, (SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE));
	SDL_Texture *texture = NULL;
	SDL_Surface *surface;	

	// create target texture for drawing segment
	SDL_Rect *segment_rect = SDL_reinterpret_cast(SDL_Rect*, nisetro->segment_bg_surface_->userdata);
	SDL_Texture *segment_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, segment_rect->w, segment_rect->h);
	SDL_SetTextureScaleMode(segment_texture, SDL_ScaleModeBest);
	SDL_SetTextureBlendMode(segment_texture, SDL_BLENDMODE_BLEND);

	// texture for segment bg
	SDL_Texture *segment_bg_texture = SDL_CreateTextureFromSurface(renderer, nisetro->segment_bg_surface_);
	SDL_SetTextureScaleMode(segment_bg_texture, SDL_ScaleModeBest);
	SDL_SetTextureBlendMode(segment_bg_texture, SDL_BLENDMODE_BLEND);
	SDL_SetTextureColorMod(segment_bg_texture, 0x18, 0x18, 0x18);
	SDL_SetTextureAlphaMod(segment_bg_texture, 0xff);

	// texture for segment icon
	SDL_Texture *segment_icon_texture = SDL_CreateTextureFromSurface(renderer, nisetro->segment_icon_surface_);
	SDL_SetTextureScaleMode(segment_icon_texture, SDL_ScaleModeBest);
	SDL_SetTextureBlendMode(segment_icon_texture, SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_DST_ALPHA, SDL_BLENDFACTOR_ZERO, SDL_BLENDOPERATION_ADD,
																			 SDL_BLENDFACTOR_DST_ALPHA, SDL_BLENDFACTOR_ZERO, SDL_BLENDOPERATION_ADD));

	while (nisetro->keep_running_)
	{
		SDL_LockMutex(nisetro->render_thread_mutex_);

		// frame interval is (256 x 159) / 3072000 = 13.25ms
		// so we just set timeout to about 4x of frame interval
		render_wait_timeout = SDL_CondWaitTimeout(nisetro->render_thread_cond_, nisetro->render_thread_mutex_, 50);

		SDL_AtomicTryLock(&nisetro->render_params_lock_);
		{
			if (nisetro->render_params_dirty_)
			{
				// recreate texture if any of texture setting changed
				if (backbuffer_size != nisetro->setting_->getVideoBackBufferSize() ||
					texture_scale_mode != nisetro->setting_->getVideoTextureScaleMode() ||
					render_scale_quality != nisetro->setting_->getVideoTextureFilter())
				{
					backbuffer_size = nisetro->setting_->getVideoBackBufferSize();
					texture_scale_mode = nisetro->setting_->getVideoTextureScaleMode();
					render_scale_quality = nisetro->setting_->getVideoTextureFilter();

					frame_rect.w = VIDEO_FRAME_VALID_WIDTH * backbuffer_size;
					frame_rect.h = VIDEO_FRAME_VALID_LINES * backbuffer_size;

					if (texture)
						SDL_DestroyTexture(texture);

					// create texture for rendering
					texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, getNextPowerOfTwo(frame_rect.w), getNextPowerOfTwo(frame_rect.h));
					SDL_SetTextureScaleMode(texture, (SDL_ScaleMode)texture_scale_mode);
					SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
					
					// clear texture
					SDL_LockTextureToSurface(texture, NULL, &surface);
					SDL_FillRect(surface, NULL, 0);
					SDL_UnlockTexture(texture);
				}

				frame_copy_frect = nisetro->frame_copy_frect_;
				segment_copy_frect = nisetro->segment_copy_frect_;
				render_rotate_angle = nisetro->render_rotate_angle_;

				setRendererLogicalSize(renderer, nisetro->window_orientation_);
				nisetro->render_params_dirty_ = false;
			}
		}
		SDL_AtomicUnlock(&nisetro->render_params_lock_);

		// only update texture when render surface is updated in time
		if (render_wait_timeout == 0)
		{
			segment_state = *(SDL_reinterpret_cast(uint16_t*, nisetro->render_surface_->pixels) + VIDEO_FRAME_VALID_WIDTH);

			SDL_LockTextureToSurface(texture, NULL, &surface);
			SDL_BlitScaled(nisetro->render_surface_, &NisetroPreviewSDL::renderSurfaceRect, surface, &frame_rect);
			SDL_UnlockTexture(texture);
		}

		// screenshot
		if (SDL_AtomicCAS(&nisetro->request_screenshot_, SDL_TRUE, SDL_FALSE))
		{
			SDL_Event screenshot_event;
			screenshot_event.user.type = SDL_USEREVENT;
			screenshot_event.user.code = 513;
			screenshot_event.user.data1 = SDL_ConvertSurfaceFormat(nisetro->render_surface_, SDL_PIXELFORMAT_XRGB8888, 0);
			screenshot_event.user.data2 = reinterpret_cast<void*>(nisetro->screen_orientation_);

			SDL_PushEvent(&screenshot_event);
		}

		SDL_UnlockMutex(nisetro->render_thread_mutex_);

		// workaround for this error message
		// ERROR: SDL failed to get a vertex buffer for this Direct3D 9 rendering batch!
		// ERROR: Dropping back to a slower method.
		// ERROR : This might be a brief hiccup, but if performance is bad, this is probably why.
		// ERROR : This error will not be logged again for this renderer.
		// 
		// when set render target to texture first time, there is nothing to render (cmd is SDL_RENDERCMD_SETVIEWPORT)
		// so renderer->vertex_data_used will be zero which makes vbo keeps NULL
		// which makes later vbo null check be true
		// workaround here is just drawing something needs vbo so the vertex size will not be zero
		SDL_RenderFillRect(renderer, NULL);

		// render LCD segments
		// if (segment_state != last_segment_state)
		{
			segment_mask_count = 0;

			for (int32_t i = 2; i < 15; i++)
			{
				if ((segment_state & (1 << i)))
					segment_mask[segment_mask_count++] = NisetroPreviewSDL::segment_mask_rects[i];
			}

			if (segment_state & (1 << 8))
				segment_mask[segment_mask_count++] = NisetroPreviewSDL::segment_mask_rects[15];

			SDL_SetRenderTarget(renderer, segment_texture);

			// clear target texture
			SDL_SetRenderDrawColor(renderer, 0x00, 0x00, 0x00, 0x00);
			SDL_RenderClear(renderer);

			// draw rects with alpha value 255 where we want to showing out icon
			SDL_SetRenderDrawColor(renderer, 0x00, 0x00, 0x00, 0xff);
			SDL_RenderFillRects(renderer, segment_mask, segment_mask_count);

			// draw icon texture
			SDL_RenderCopy(renderer, segment_icon_texture, NULL, segment_rect);
			SDL_SetRenderTarget(renderer, NULL);

			last_segment_state = segment_state;
		}

		SDL_SetRenderDrawColor(renderer, 0x00, 0x00, 0x00, 0xff);
		SDL_RenderClear(renderer);
		
		SDL_RenderCopyExF(renderer, segment_bg_texture, segment_rect, &segment_copy_frect, render_rotate_angle, NULL, SDL_FLIP_NONE);
		SDL_RenderCopyExF(renderer, segment_texture, segment_rect, &segment_copy_frect, render_rotate_angle, NULL, SDL_FLIP_NONE);
		SDL_RenderCopyExF(renderer, texture, &frame_rect, &frame_copy_frect, render_rotate_angle, NULL, SDL_FLIP_NONE);
		SDL_RenderPresent(renderer);
	}

	SDL_DestroyTexture(texture);
	SDL_DestroyTexture(segment_texture);
	SDL_DestroyTexture(segment_bg_texture);
	SDL_DestroyTexture(segment_icon_texture);
	SDL_DestroyRenderer(renderer);

	SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "render thread ended");

	return 0;
}

int NisetroPreviewSDL::videoThreadProc(void *userdata)
{
	SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "video thread started");
	SDL_SetThreadPriority(SDL_THREAD_PRIORITY_NORMAL);

	NisetroPreviewSDL *nisetro = reinterpret_cast<NisetroPreviewSDL*>(userdata);
	ringbuffer *rb = &nisetro->capture_ringbuffer_;

	uint8_t captured_data[BUF_LEN];
	uint32_t elements_read;

	nisetro->video_frame_count_ = 0;
	nisetro->frame_synced_ = false;
	nisetro->last_sync_state_ = 0;
	nisetro->lcd_segment_state_ = 0x0000;

	// clean up surface
	SDL_FillRect(nisetro->render_surface_, NULL, 0);
	SDL_FillRect(nisetro->video_frame_surface_, NULL, 0);

	while (nisetro->keep_running_ /* TODO use better check */)
	{
		SDL_LockMutex(nisetro->video_thread_mutex_);

		if (ringbuffer_is_empty(rb))
			SDL_CondWait(nisetro->video_thread_cond_, nisetro->video_thread_mutex_);

		elements_read = ringbuffer_read(rb, captured_data, sizeof(captured_data) / sizeof(uint16_t));

		SDL_UnlockMutex(nisetro->video_thread_mutex_);

		if (elements_read > 0)
			nisetro->processCaptureData(reinterpret_cast<uint16_t*>(captured_data), elements_read);
	}

	SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "video thread ended");

	return 0;
}

void NisetroPreviewSDL::onSDLRequestAudioData(void *userdata, uint8_t *stream, int len)
{
	NisetroPreviewSDL *nisetro = reinterpret_cast<NisetroPreviewSDL*>(userdata);
	int32_t bytes_read = SDL_AudioStreamGet(nisetro->audio_out_stream_, stream, len);

	if (bytes_read != len)
	{
		if (bytes_read < 0)
			bytes_read = 0;

		SDL_memset(stream + bytes_read, 0, len - bytes_read);
		SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "%8u: not enough audio data %d / %d", SDL_GetTicks(), len - bytes_read, len);
	}
}

int NisetroPreviewSDL::audioThreadProc(void *userdata)
{
	SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "audio thread started");
	SDL_SetThreadPriority(SDL_THREAD_PRIORITY_NORMAL);

	NisetroPreviewSDL *nisetro = reinterpret_cast<NisetroPreviewSDL*>(userdata);
	ringbuffer *rb = &nisetro->audio_ringbuffer_;
	AudioSample audio_samples[(AUDIO_PROCESS_BUFFER_SIZE >> 2) + 2];
	float resampled_samples[AUDIO_PROCESS_BUFFER_SIZE];
	
	SDL_AudioCVT cvt;
	SRC_STATE *src_state = src_new(nisetro->setting_->getAudioResampleQuality(), 2, NULL);
	SRC_DATA src_data;

	AudioSample *mixed_samples;
	uint32_t samples_read, bytes_read;
	int32_t wait_timeout;
	
	int32_t samples_streamed = 0;
	int32_t enable_interpolation = nisetro->audio_enable_interpolation_;
	int32_t volume = nisetro->audio_volume_;

	// large BCLK -> less samples created -> run out of samples if too large
	// small BCLK -> more samples created -> delay happens in the long run
	// TODO find a nice way to adjust ratio on the fly based on buffered audio sample
	double input_frequency = nisetro->setting_->getAudioInputFrequency();

	audio_samples[0].sample = audio_samples[1].sample = 0;

	// setting up audio cvt for converting data type from ws native format to float
	SDL_BuildAudioCVT(&cvt, AUDIO_NATIVE_FORMAT, 2, 1, AUDIO_F32SYS, 2, 1);
	cvt.buf = reinterpret_cast<uint8_t*>(SDL_malloc(cvt.len_mult * AUDIO_PROCESS_BUFFER_SIZE));

	SDL_memset(&src_data, 0, sizeof(src_data));
	src_data.src_ratio = nisetro->audio_spec_.freq / (input_frequency / 32.0);
	src_data.data_out = resampled_samples;
	src_data.output_frames = (AUDIO_PROCESS_BUFFER_SIZE >> 1);

	// clean up audio stream
	SDL_AudioStreamClear(nisetro->audio_out_stream_);

	// force update parameters once
	nisetro->audioplay_params_dirty_ = true;

	SDL_LogInfo(SDL_LOG_CATEGORY_AUDIO, "Using audio input frequency: %6.3f, ratio = %.8f", input_frequency, src_data.src_ratio);

	while (nisetro->keep_running_)
	{
		SDL_LockMutex(nisetro->audio_thread_mutex_);

		wait_timeout = (ringbuffer_is_empty(rb) ? SDL_CondWaitTimeout(nisetro->audio_thread_cond_, nisetro->audio_thread_mutex_, 50) : 0);

		// fills new sample start from index 2 for later interpolation
		if (wait_timeout == 0)
			samples_read = ringbuffer_read(rb, reinterpret_cast<uint8_t*>(audio_samples + 2), (AUDIO_PROCESS_BUFFER_SIZE >> 2));

		SDL_UnlockMutex(nisetro->audio_thread_mutex_);

		// not receiving audio data for too long... assume device is off or resetting
		if (wait_timeout == SDL_MUTEX_TIMEDOUT)
		{
			ringbuffer_clear(rb);
			src_reset(src_state);

			SDL_LockAudioDevice(nisetro->audio_device_id_);
			SDL_PauseAudioDevice(nisetro->audio_device_id_, 1);
			SDL_AudioStreamClear(nisetro->audio_out_stream_);
			SDL_UnlockAudioDevice(nisetro->audio_device_id_);

			samples_streamed = 0;
			continue;
		}

		if (samples_read <= 0)
			continue;

		bytes_read = (samples_read << 2);
	
		// fast check if audio parameters changed
		SDL_AtomicTryLock(&nisetro->audioplay_params_lock_);
		{
			if (nisetro->audioplay_params_dirty_)
			{
				enable_interpolation = nisetro->audio_enable_interpolation_;
				volume = nisetro->audio_volume_;

				nisetro->audioplay_params_dirty_ = false;
			}
		}
		SDL_AtomicUnlock(&nisetro->audioplay_params_lock_);

		// Simulate BU9480F interpolation
		if (enable_interpolation)
		{
			for (uint32_t i = 0; i < samples_read; i++)
			{
				audio_samples[i].left = ((audio_samples[i].left + audio_samples[i + 1].left) / 2);
				audio_samples[i].right = ((audio_samples[i].right + audio_samples[i + 1].right) / 2);
			}

			mixed_samples = audio_samples;
		}
		else
		{
			mixed_samples = audio_samples + 2;
		}

		// adjust volume if needed
		if (volume != SDL_MIX_MAXVOLUME)
		{
			SDL_memset4(cvt.buf, 0, samples_read);

			if (volume > 0)
				SDL_MixAudioFormat(cvt.buf, reinterpret_cast<uint8_t*>(mixed_samples), AUDIO_NATIVE_FORMAT, bytes_read, volume);
		}
		else
		{
			// if no volume adjust, just copy audio samples
			SDL_memcpy4(cvt.buf, mixed_samples, samples_read);
		}

		// convert to F32SYS for later resampling
		cvt.len = bytes_read;
		SDL_ConvertAudio(&cvt);

		// 8 bytes per frame
		src_data.input_frames = (cvt.len_cvt >> 3);
		src_data.data_in = reinterpret_cast<const float*>(cvt.buf);

		// make sure we have processed all the input frames
		while (src_data.input_frames > 0)
		{
			src_process(src_state, &src_data);

			if (src_data.output_frames_gen > 0)
			{
				SDL_LockAudioDevice(nisetro->audio_device_id_);
				SDL_AudioStreamPut(nisetro->audio_out_stream_, src_data.data_out, (src_data.output_frames_gen << 3));
				SDL_UnlockAudioDevice(nisetro->audio_device_id_);

				if (samples_streamed >= 0)
				{
					samples_streamed += src_data.output_frames_gen;

					if (samples_streamed > 1024)
					{
						SDL_PauseAudioDevice(nisetro->audio_device_id_, 0);
						samples_streamed = -1;
					}
				}
			}

			src_data.data_in += (src_data.input_frames_used << 1);
			src_data.input_frames -= src_data.input_frames_used;
		}

		// TODO save wav here (mixed_samples)

		// just copies last two samples to front of buffer for easy interpolation enable switching
		audio_samples[0].sample = audio_samples[samples_read].sample;
		audio_samples[1].sample = audio_samples[samples_read + 1].sample;
	}

	SDL_PauseAudioDevice(nisetro->audio_device_id_, 1);
	SDL_free(cvt.buf);
	src_delete(src_state);
	SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "audio thread ended");

	return 0;
}

bool NisetroPreviewSDL::cusbThreadProc(u8 *buf, u32 buf_len, cusb2_tcb *tcb)
{
	NisetroPreviewSDL *nisetro = reinterpret_cast<NisetroPreviewSDL*>(tcb->userpointer);
	uint32_t elements_written;
	buf_len = (buf_len >> 1);	// 2bytes per pixel data

	SDL_LockMutex(nisetro->video_thread_mutex_);

	// lock and write to ringbuffer
	if ((elements_written = ringbuffer_write(&nisetro->capture_ringbuffer_, buf, buf_len)) != buf_len)
		SDL_LogWarn(SDL_LOG_CATEGORY_SYSTEM, "not enough capture ringbuffer to write %u / %u", elements_written, buf_len);

	// notify video thread
	SDL_CondSignal(nisetro->video_thread_cond_);
	SDL_UnlockMutex(nisetro->video_thread_mutex_);

	return tcb->looping;
}

bool NisetroPreviewSDL::cusbAudioThreadProc(u8 *buf, u32 buf_len, cusb2_tcb *tcb)
{
	NisetroPreviewSDL *nisetro = reinterpret_cast<NisetroPreviewSDL*>(tcb->userpointer);
	uint32_t samples_written;
	buf_len = (buf_len >> 2);	// 4bytes per audio sample

	SDL_LockMutex(nisetro->audio_thread_mutex_);

	// if there is too many samples pending for processing, just discard them so we can always hear latest audio samples
	if (ringbuffer_get_available(&nisetro->audio_ringbuffer_) < buf_len)
	{
		ringbuffer_discard(&nisetro->audio_ringbuffer_, buf_len);
		SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "discard %d samples in ringbuffer", buf_len);
	}

	samples_written = ringbuffer_write(&nisetro->audio_ringbuffer_, buf, buf_len);

	// notify audio thread
	SDL_CondSignal(nisetro->audio_thread_cond_);
	SDL_UnlockMutex(nisetro->audio_thread_mutex_);

	return tcb->looping;
}

void NisetroPreviewSDL::setWindowSize(int32_t size)
{
	int width, height;

	// TODO check full screen
	// TODO check size range

	switch (window_orientation_)
	{
		case 1:	// vertical
		{
			width = VIDEO_FRAME_LOGICAL_HEIGHT * size;
			height = VIDEO_FRAME_LOGICAL_WIDTH * size;
			break;
		}
		case 2:	// horizontal
		{
			width = VIDEO_FRAME_LOGICAL_WIDTH * size;
			height = VIDEO_FRAME_LOGICAL_HEIGHT * size;
			break;
		}
		case 3:	// kerorican
		{
			width = VIDEO_FRAME_LOGICAL_WIDTH * size;
			height = VIDEO_FRAME_LOGICAL_HEIGHT * size;
			NisetroPreviewSDL::getKeroricanWindowSize(width, height, &width, &height);
			break;
		}
		default: return;
	}

	SDL_SetWindowSize(window_, width, height);
}

void NisetroPreviewSDL::setWindowRotateMethod(int32_t method)
{
	if (window_rotate_method_ == method)
		return;

	window_rotate_method_ = method;

	switch (method)
	{
		case 0:
		{
			// use real orientation
			setWindowOrientation(screen_orientation_);
			break;
		}
		case 1:
		case 2:
		case 3:
		{
			// force orientation to vertical or horizontal or kerorican
			setWindowOrientation(method);
			break;
		}
		default: return;
	}
}

void NisetroPreviewSDL::setVideoBackBufferSize(int32_t size)
{
	SDL_AtomicLock(&render_params_lock_);
	{
		if (setting_->setVideoBackBufferSize(size))
			render_params_dirty_ = true;
	}
	SDL_AtomicUnlock(&render_params_lock_);
}

void NisetroPreviewSDL::setVideoTextureScaleMode(int32_t mode)
{
	SDL_AtomicLock(&render_params_lock_);
	{
		if (setting_->setVideoTextureScaleMode(mode))
			render_params_dirty_ = true;
	}
	SDL_AtomicUnlock(&render_params_lock_);
}

void NisetroPreviewSDL::setVideoTextureFilter(int32_t filter)
{
	SDL_AtomicLock(&render_params_lock_);
	{
		if (setting_->setVideoTextureFilter(filter))
			render_params_dirty_ = true;
	}
	SDL_AtomicUnlock(&render_params_lock_);
}

void NisetroPreviewSDL::setAudioDeviceName(const char *audio_device_name)
{
	reopenAudioDevice(audio_device_name);
}

bool NisetroPreviewSDL::reopenAudioDevice(const char *device_name)
{
	bool device_name_changed = false;

	if (audio_device_name_ && device_name)
	{
		if (SDL_strcmp(audio_device_name_, device_name))
			device_name_changed = true;
	}
	else if (audio_device_name_ != device_name)
	{
		device_name_changed = true;
	}

	// nothing changed
	if (!device_name_changed && audio_device_id_ > 0)
		return false;

	// TODO check need to lock audio device??
	if (audio_device_id_ > 0)
	{
		SDL_PauseAudioDevice(audio_device_id_, 1);
		SDL_CloseAudioDevice(audio_device_id_);
		audio_device_id_ = 0;
	}

	// recreate audio device
	SDL_AudioSpec desired_audio_spec;
	SDL_memset(&desired_audio_spec, 0, sizeof(desired_audio_spec));
	SDL_memset(&audio_spec_, 0, sizeof(audio_spec_));

	desired_audio_spec.freq = 48000;
	desired_audio_spec.format = AUDIO_F32SYS;
	desired_audio_spec.channels = 2;
	desired_audio_spec.samples = AUDIO_DEVICE_BUFFER_SAMPLES;
	desired_audio_spec.callback = NisetroPreviewSDL::onSDLRequestAudioData;
	desired_audio_spec.userdata = this;

	if ((audio_device_id_ = SDL_OpenAudioDevice(device_name, 0, &desired_audio_spec, &audio_spec_, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE)) == 0)
	{
		SDL_LogError(SDL_LOG_CATEGORY_SYSTEM, "Unable to open audio device: %s", SDL_GetError());
		return false;
	}

	// recreate audio stream if output sample rate changed
	if (audio_out_stream_)
	{
		SDL_AudioStreamClear(audio_out_stream_);
		SDL_FreeAudioStream(audio_out_stream_);
		audio_out_stream_ = NULL;
	}

	if ((audio_out_stream_ = SDL_NewAudioStream(AUDIO_F32SYS, 2, audio_spec_.freq, audio_spec_.format, audio_spec_.channels, audio_spec_.freq)) == NULL)
	{
		SDL_LogError(SDL_LOG_CATEGORY_SYSTEM, "Unable to open audio stream: %s", SDL_GetError());
		return false;
	}

	if (device_name_changed)
	{
		SDL_free(audio_device_name_);
		audio_device_name_ = (device_name ? SDL_strdup(device_name) : NULL);
	}

	return true;
}

void NisetroPreviewSDL::setAudioEnableInterpolation(int32_t enabled)
{
	SDL_AtomicLock(&audioplay_params_lock_);
	{
		if (audio_enable_interpolation_ < 0 || (audio_enable_interpolation_ ^ enabled))
		{
			audio_enable_interpolation_ = (enabled ? 1 : 0);
			audioplay_params_dirty_ = true;

			setting_->setAudioInterpolationEnabled((audio_enable_interpolation_ != 0));
		}
	}
	SDL_AtomicUnlock(&audioplay_params_lock_);
}

void NisetroPreviewSDL::setAudioMute(bool enabled)
{
	setAudioVolume(SDL_copysignf(setting_->getAudioVolume(), (enabled ? -1.0f : 1.0f)));
}

void NisetroPreviewSDL::setAudioVolume(float volume)
{
	int32_t sdl_volume = (int32_t)(volume * SDL_MIX_MAXVOLUME);

	// clamp range, minus volume means mute, but still keeps its volume value
	if (sdl_volume < -SDL_MIX_MAXVOLUME)
		sdl_volume = -SDL_MIX_MAXVOLUME;

	if (sdl_volume > SDL_MIX_MAXVOLUME)
		sdl_volume = SDL_MIX_MAXVOLUME;

	SDL_AtomicLock(&audioplay_params_lock_);
	{
		if (sdl_volume != audio_volume_)
		{
			audio_volume_ = sdl_volume;
			audioplay_params_dirty_ = true;

			setting_->setAudioVolume(volume);
		}
	}
	SDL_AtomicUnlock(&audioplay_params_lock_);
}

void NisetroPreviewSDL::setAudioInputFrequency(double frequency)
{
	SDL_AtomicLock(&audioplay_params_lock_);
	{
		setting_->setAudioInputFrequency(frequency);
		audioplay_params_dirty_ = true;
	}
	SDL_AtomicUnlock(&audioplay_params_lock_);
}

void NisetroPreviewSDL::setWindowOrientation(int32_t orientation)
{
	// 1: Vertical, 2: Horizontal, 3: Kerorican
	if (window_orientation_ == orientation)
		return;

	int width, height;

	// TODO full screen?

	if (orientation < 3)
	{
		if (window_orientation_ == 3)
		{
			// set window from kerorican to horizontal / vertical
			// calculate original window size
			if (orientation == 2)
				NisetroPreviewSDL::getOriginalWindowSize(window_width_, window_height_, &width, &height);
			else
				NisetroPreviewSDL::getOriginalWindowSize(window_width_, window_height_, &height, &width);
		}
		else
		{
			// switch between horizontal and vertical
			width = window_height_;
			height = window_width_;
		}
	}
	else
	{
		// switch from horizontal/vertical to kerorican
		if (window_orientation_ == 2)
			NisetroPreviewSDL::getKeroricanWindowSize(window_width_, window_height_, &width, &height);
		else
			NisetroPreviewSDL::getKeroricanWindowSize(window_height_, window_width_, &width, &height);
	}

	SDL_AtomicLock(&render_params_lock_);
	{
		window_orientation_ = orientation;
		render_params_dirty_ = true;
	}
	SDL_AtomicUnlock(&render_params_lock_);

	SDL_SetWindowSize(window_, width, height);
}

void NisetroPreviewSDL::getKeroricanWindowSize(int32_t original_width, int32_t original_height, int32_t *kerorican_width, int32_t *kerorican_height)
{
	*kerorican_width = (int32_t)(original_width * KERORICAN_ROTATE_COSINE + original_height * KERORICAN_ROTATE_SINE + 0.5);
	*kerorican_height = (int32_t)(original_height * KERORICAN_ROTATE_COSINE + original_width * KERORICAN_ROTATE_SINE + 0.5);
}

void NisetroPreviewSDL::getOriginalWindowSize(int32_t kerorican_width, int32_t kerorican_height, int32_t *original_width, int32_t *original_height)
{
	*original_width = (int32_t)(((kerorican_width * (1.0 - KERORICAN_FRAME_RATIO)) / KERORICAN_ROTATE_COSINE) + 0.5);
	*original_height = (int32_t)(((kerorican_width * KERORICAN_FRAME_RATIO) / KERORICAN_ROTATE_SINE) + 0.5);
}

void NisetroPreviewSDL::setRendererLogicalSize(SDL_Renderer *renderer, int32_t orientation)
{
	int w, h;

	switch (orientation)
	{
		case 1: w = VIDEO_FRAME_LOGICAL_HEIGHT; h = VIDEO_FRAME_LOGICAL_WIDTH; break;
		case 2: w = VIDEO_FRAME_LOGICAL_WIDTH; h = VIDEO_FRAME_LOGICAL_HEIGHT; break;
		case 3: w = KERORICAN_FRAME_LOGICAL_WIDTH; h = KERORICAN_FRAME_LOGICAL_HEIGHT; break;
	}

	SDL_RenderSetLogicalSize(renderer, w, h);
}

size_t NisetroPreviewSDL::getLocalTimeString(char *buf, size_t buf_size)
{
	if (buf == NULL || buf_size <= 0)
		return 0;

	time_t time_sec;
	long time_msec;
	size_t ret = 0;

#ifdef  _POSIX_
	// TODO use gettimeofday
#else
	FILETIME file_time;
	ULARGE_INTEGER ularge;

	GetSystemTimeAsFileTime(&file_time);
	ularge.LowPart = file_time.dwLowDateTime;
	ularge.HighPart = file_time.dwHighDateTime;

	time_sec = (time_t)((ularge.QuadPart - DELTA_EPOCH_IN_MICROSECS) / FILETIME_UNITS_PER_SEC);
	time_msec = (long)((((ularge.QuadPart - DELTA_EPOCH_IN_MICROSECS) % FILETIME_UNITS_PER_SEC) / FILETIME_UNITS_PER_USEC) / 1000);
#endif

	tm local_tm = *localtime(&time_sec);
	ret = strftime(buf, buf_size, "%Y%m%d%H%M%S", &local_tm);
	buf_size -= ret;

	if (buf_size > 3)
	{
		buf[14] = (char)('0' + (time_msec / 100));
		buf[15] = (char)('0' + (time_msec % 100) / 10);
		buf[16] = (char)('0' + (time_msec % 10));
		buf[17] = '\0';

		ret += 3;
	}

	return ret;
}

int32_t NisetroPreviewSDL::getNextPowerOfTwo(int32_t value)
{
	--value;

	value |= value >> 1;
	value |= value >> 2;
	value |= value >> 4;
	value |= value >> 8;
	value |= value >> 16;
	
	++value;

	return value;
}
