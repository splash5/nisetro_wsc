#ifndef __NISETRO_PREVIEW_SDL_H__
#define __NISETRO_PREVIEW_SDL_H__

#include "SDL.h"

#include "cfx2log.h"
#include "../windows/cusb/cusb2.h"

#include "ringbuffer/ringbuffer.h"
#include "NisetroPreviewSDLSetting.h"

#define USB_CFG_BUF_SIZE	64

#define VIDEO_FRAME_WIDTH			256
#define VIDEO_FRAME_LINES			159
#define VIDEO_FRAME_VALID_WIDTH		224
#define VIDEO_FRAME_VALID_LINES		144
#define VIDEO_FRAME_SEGMENT_WIDTH	16
#define VIDEO_FRAME_PIXELS			VIDEO_FRAME_LINES * VIDEO_FRAME_WIDTH
#define AUDIO_SAMPLES_PER_FRAME		VIDEO_FRAME_LINES * 2

#define VIDEO_FRAME_LOGICAL_WIDTH	240
#define VIDEO_FRAME_LOGICAL_HEIGHT	144
#define KERORICAN_FRAME_LOGICAL_WIDTH	279		// 240 * KERORICAN_ROTATE_COSINE + 144 * KERORICAN_ROTATE_SINE
#define KERORICAN_FRAME_LOGICAL_HEIGHT	236		// 240 * KERORICAN_ROTATE_SINE + 144 * KERORICAN_ROTATE_COSINE

#define VIDEO_TEXTURE_DEFAULT_SIZE	256
#define AUDIO_NATIVE_FORMAT			AUDIO_S16LSB

#define LCD_SEGMENT_ENABLED			0x0001
#define LCD_SEGMENT_POWER			0x0004
#define LCD_SEGMENT_CARTRIDGE		0x0008
#define LCD_SEGMENT_SLEEP			0x0010
#define LCD_SEGMENT_LOW_BATTERY		0x0020
#define LCD_SEGMENT_VOLUME_0		0x0040
#define LCD_SEGMENT_VOLUME_1		0x0080
#define LCD_SEGMENT_VOLUME_2		0x0100
#define LCD_SEGMENT_HEADPHONE		0x0200
#define LCD_SEGMENT_VERTICAL		0x0400
#define LCD_SEGMENT_HORIZONTAL		0x0800
#define LCD_SEGMENT_DOT_1			0x1000
#define LCD_SEGMENT_DOT_2			0x2000
#define LCD_SEGMENT_DOT_3			0x4000

// arctan(0.5)
#define KERORICAN_ROTATE_DEGREE		26.56505118
#define KERORICAN_ROTATE_SINE		0.4472135954999579392818347337462
#define KERORICAN_ROTATE_COSINE		0.8944271909999158785636694674925
#define KERORICAN_FRAME_RATIO		(VIDEO_FRAME_LOGICAL_HEIGHT.0f / (VIDEO_FRAME_LOGICAL_WIDTH.0f * 2.0f + VIDEO_FRAME_LOGICAL_HEIGHT.0f))

typedef union AudioSample
{
	struct
	{
		int16_t left;
		int16_t right;
	};

	uint32_t sample;

} AudioSample;

class NisetroPreviewSDL
{
public:
	NisetroPreviewSDL(SDL_Window *window, NisetroPreviewSDLSetting *setting);
	~NisetroPreviewSDL(void);

	bool init(void);

private:
	void closeUSB(void);
	bool resetUSB(uint8_t usb_device_id, void *userdata);
	bool isCFX2LogExists(void);

	void processCaptureData(uint16_t *data, uint32_t elemet_count);

	static int videoThreadProc(void *userdata);
	static int audioThreadProc(void *userdata);

	static int renderThreadProc(void *userdata);
	static void onSDLRequestAudioData(void *userdata, uint8_t *stream, int len);
private:
	SDL_Window *window_;
	NisetroPreviewSDLSetting *setting_;

	bool keep_running_;
	SDL_Thread *render_thread_;
	SDL_Thread *video_thread_;
	SDL_Thread *audio_thread_;

	SDL_mutex *render_thread_mutex_;
	SDL_mutex *video_thread_mutex_;
	SDL_mutex *audio_thread_mutex_;

	SDL_cond *render_thread_cond_;
	SDL_cond *video_thread_cond_;
	SDL_cond *audio_thread_cond_;

	SDL_SpinLock render_params_lock_;
	SDL_SpinLock audioplay_params_lock_;
	bool render_params_dirty_;
	bool audioplay_params_dirty_;

	SDL_atomic_t request_screenshot_;
	
	SDL_AudioDeviceID audio_device_id_;
	SDL_AudioSpec audio_spec_;
	SDL_AudioStream *audio_out_stream_;
	ringbuffer audio_ringbuffer_;
	uint8_t audio_buffer_[BUF_LEN * QUE_NUM];

	SDL_Surface *video_frame_surface_;
	SDL_Surface *render_surface_;
	uint8_t capture_buffer_[BUF_LEN * QUE_NUM];
	ringbuffer capture_ringbuffer_;

	bool frame_synced_;
	uint16_t last_sync_state_;

	uint32_t video_line_pixels_;
	uint32_t video_frame_lines_;
	uint32_t video_frame_pixels_;
	uint32_t video_frame_count_;

	uint8_t *line_pixels_;
	uint16_t *pixels_;
	
private:
	bool window_maximized_;
	int32_t window_width_;
	int32_t window_height_;
	int32_t window_rotate_method_;	// 0: auto, 1: vertical, 2: horizontal, 3: kerorican (arctan(0.5))
	int32_t window_orientation_;

	SDL_FRect frame_copy_frect_;	// rect for copy frame texture to renderer
	double render_rotate_angle_;	// output rotate
	
	char *audio_device_name_;
	int32_t audio_enable_interpolation_;
	int32_t audio_volume_;
	double audio_input_frequency_;

	// update by segment update, 1: Vertical, 2: Horizontal
	int32_t screen_orientation_;
	uint16_t lcd_segment_state_;

private:
	static const SDL_Rect frameSurfaceRect;
	static const SDL_Rect renderSurfaceRect;
	static const uint64_t performanceFrequency;
	
	static NisetroPreviewSDLSetting defaultSetting;

private:
	static uint8_t fx2lp_fw_[];

	HWND hwnd_;
	cusb2 *usb_device_;
	uint8_t usb_device_id_;

	cusb2_tcb *cusb_tcb_;
	cusb2_tcb *audio_tcb_;

	CCyUSBEndPoint *usb_out_ep_;
	static bool cusbThreadProc(u8 *buf, u32 buf_len, cusb2_tcb *tcb);
	static bool cusbAudioThreadProc(u8 *buf, u32 buf_len, cusb2_tcb *tcb);

public:
	void handleWindowEvent(const SDL_WindowEvent *window_event);
	void handleUserEvent(const SDL_UserEvent *user_event);
	void handleDeviceChangeEvent(HWND hWnd, WPARAM wParam, LPARAM lParam, uint8_t usb_device_id);

public:
	void setWindowSize(int32_t size);
	void setWindowRotateMethod(int32_t method);
	void setWindowOrientation(int32_t orientation);

	void setAudioDeviceName(const char *device_name);
	void setAudioEnableInterpolation(int32_t enabled);
	void setAudioVolume(float volume);
	void setAudioMute(bool enabled);
	void setAudioInputFrequency(double frequency);

	bool reopenAudioDevice(const char *device_name);

	void requestScreenshot(void);

private:
	static void setRendererLogicalSize(SDL_Renderer *renderer, int32_t orientation);
	static void getKeroricanWindowSize(int32_t original_width, int32_t original_height, int32_t *kerorican_width, int32_t *kerorican_height);
	static void getOriginalWindowSize(int32_t kerorican_width, int32_t kerorican_height, int32_t *original_width, int32_t *original_height);

	static size_t getLocalTimeString(char *buf, size_t buf_size);
	static int32_t getNextPowerOfTwo(int32_t value);
};
#endif
