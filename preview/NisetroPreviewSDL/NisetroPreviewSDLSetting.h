#ifndef __NISETRO_PREVIEW_SDL_SETTING_H__
#define __NISETRO_PREVIEW_SDL_SETTING_H__

#include "SDL.h"
#include "jsoncpp/json/json.h"

class NisetroPreviewSDLSetting
{
public:
	NisetroPreviewSDLSetting(void);
	NisetroPreviewSDLSetting(const char *file_path);
	~NisetroPreviewSDLSetting(void);

private:
	void loadFromJsonValue(Json::Value &json_value);

public:
	SDL_FORCE_INLINE bool getAudioEnabled(void) { return audio_enabled_; }
	SDL_FORCE_INLINE const char *getAudioDeviceName(void) { return audio_device_name_; }
	SDL_FORCE_INLINE double getAudioInputFrequency(void) { return audio_input_frequency_; }
	SDL_FORCE_INLINE int32_t getAudioResampleQuality(void) { return audio_resample_quality_; }
	SDL_FORCE_INLINE int32_t getAudioBufferSize(void) { return audio_buffer_size_; }
	SDL_FORCE_INLINE int32_t getAudioBufferCount(void) { return audio_buffer_count_; }
	SDL_FORCE_INLINE bool getAudioInterpolationEnabled(void) { return audio_interpolation_enabled_; }
	SDL_FORCE_INLINE float getAudioVolume(void) { return audio_volume_; }

	SDL_FORCE_INLINE int32_t getVideoBackBufferSize(void) { return video_backbuffer_size_; }
	SDL_FORCE_INLINE const char *getVideoTextureFilter(void) { return NisetroPreviewSDLSetting::textureScaleQuality[video_texture_filter_]; }
	SDL_FORCE_INLINE SDL_ScaleMode getVideoTextureScaleMode(void) { return (SDL_ScaleMode)video_texture_scale_mode_; }

	SDL_FORCE_INLINE const char *getScreenshotPath(void) { return screenshot_path_; }
	SDL_FORCE_INLINE uint8_t getUSBDeviceID(void) { return usb_device_id_; }

	SDL_FORCE_INLINE bool getDebugConsoleEnabled(void) { return debug_console_enabled_; }
	SDL_FORCE_INLINE bool getDisableScreenSaver(void) { return disable_screen_saver_; }

	void setAudioEnabled(bool value);
	void setAudioDeviceName(const char *device_name);
	void setAudioInputFrequency(double frequency);
	void setAudioResampleQuality(int quality);
	void setAudioBufferSize(int buffer_size);
	void setAudioBufferCount(int buffer_count);
	void setAudioInterpolationEnabled(bool value);
	void setAudioVolume(float volume);

	void setVideoBackBufferSize(int size);
	void setVideoTextureFilter(int filter);
	void setVideoTextureScaleMode(int mode);

	void setShowLCDSegments(bool value);
	
	void setScreenshotPath(const char *path);
	void setUSBDeviceID(uint8_t usb_device_id);
	void setDebugConsoleEnabled(bool value);
	void setDisableScreenSaver(bool value);

private:
	Json::Value setting_value_;

	char *file_path_;

	char *audio_device_name_;
	double audio_input_frequency_;
	int32_t audio_resample_quality_;
	int32_t audio_buffer_size_;
	int32_t audio_buffer_count_;
	bool audio_interpolation_enabled_;
	float audio_volume_;
	bool audio_enabled_;

	int32_t video_backbuffer_size_;
	int32_t video_texture_filter_;
	int32_t video_texture_scale_mode_;
	bool show_lcd_segments_;
	
	int32_t window_width_;
	int32_t window_height_;
	int32_t window_pos_x_;
	int32_t window_pos_y_;

	uint8_t usb_device_id_;
	char *screenshot_path_;

	bool drop_error_frame_;
	bool keeps_window_topmost_;
	bool disable_screen_saver_;
	bool debug_console_enabled_;

private:
	static const char *textureScaleQuality[3];
};

#endif
