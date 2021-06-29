#include "NisetroPreviewSDLSetting.h"

#include "samplerate.h"

const char *NisetroPreviewSDLSetting::textureScaleQuality[3] =
{
	"nearest",
	"linear",
	"best"
};

NisetroPreviewSDLSetting::NisetroPreviewSDLSetting(void)
{
	screenshot_path_ = NULL;
	audio_device_name_ = NULL;

	loadFromJsonValue(setting_value_);

	file_path_ = NULL;
}

NisetroPreviewSDLSetting::NisetroPreviewSDLSetting(const char *file_path)
{
	screenshot_path_ = NULL;
	audio_device_name_ = NULL;

	SDL_RWops *setting_file = SDL_RWFromFile(file_path, "r");
	Json::Value value;

	if (setting_file)
	{
		size_t file_size = (size_t)SDL_RWsize(setting_file);
		
		if (file_size > 0)
		{
			char *content = SDL_reinterpret_cast(char*, SDL_malloc(file_size));
			file_size = SDL_RWread(setting_file, content, 1, file_size);

			Json::Reader reader;
			if (!reader.parse(content, content + file_size, value))
				SDL_LogError(SDL_LOG_CATEGORY_SYSTEM, "Unable to load setting from file %s, use default value.", file_path);

			SDL_free(content);
		}

		SDL_RWclose(setting_file);
	}
	else
	{
		SDL_LogError(SDL_LOG_CATEGORY_SYSTEM, "Unable to open setting file %s\n%s", file_path, SDL_GetError());
	}

	loadFromJsonValue(value);
	file_path_ = SDL_strdup(file_path);
}

NisetroPreviewSDLSetting::~NisetroPreviewSDLSetting(void)
{
	if (file_path_)
	{
		SDL_RWops *setting_file = SDL_RWFromFile(file_path_, "w");

		if (setting_file)
		{
			Json::StyledWriter writer;
			std::string json = writer.write(setting_value_);

			SDL_RWwrite(setting_file, json.c_str(), json.length(), 1);
			SDL_RWclose(setting_file);
		}
		else
		{
			SDL_LogError(SDL_LOG_CATEGORY_SYSTEM, "Unable to open setting file %s\n%s", file_path_, SDL_GetError());
		}

		SDL_free(file_path_);
	}

	// free all dup string
	if (audio_device_name_)
		SDL_free(audio_device_name_);

	if (screenshot_path_)
		SDL_free(screenshot_path_);
}

void NisetroPreviewSDLSetting::loadFromJsonValue(Json::Value &json_value)
{
	Json::Value value;

	setting_value_ = json_value;

	// ===========================================
	if (!!(value = json_value.get("audio_device_name", Json::Value::nullRef)) && value.isConvertibleTo(Json::stringValue))
		setAudioDeviceName(value.asCString());
	else
		setAudioDeviceName(NULL);

	// ===========================================
	if ((value = json_value.get("audio_interpolation_enabled", true)).isConvertibleTo(Json::booleanValue))
		setAudioInterpolationEnabled(value.asBool());
	else
		setAudioInterpolationEnabled(true);

	// ===========================================
	if ((value = json_value.get("audio_buffer_size", 1024)).isConvertibleTo(Json::intValue))
		setAudioBufferSize(value.asInt());
	else
		setAudioBufferSize(1024);

	// ===========================================
	if ((value = json_value.get("audio_buffer_count", 4)).isConvertibleTo(Json::intValue))
		setAudioBufferCount(value.asInt());
	else
		setAudioBufferCount(4);

	// ===========================================
	// 768527.0
	if ((value = json_value.get("audio_input_frequency", 768000.0)).isConvertibleTo(Json::realValue))
		setAudioInputFrequency(value.asDouble());
	else
		setAudioInputFrequency(768000.0);

	// ===========================================
	if ((value = json_value.get("audio_resample_quality", SRC_SINC_BEST_QUALITY)).isConvertibleTo(Json::intValue))
		setAudioResampleQuality(value.asInt());
	else
		setAudioResampleQuality(SRC_SINC_BEST_QUALITY);

	// ===========================================
	if ((value = json_value.get("audio_volume", 1.0f)).isConvertibleTo(Json::realValue))
		setAudioVolume(value.asFloat());
	else
		setAudioVolume(1.0f);

	// ===========================================
	if ((value = json_value.get("video_backbuffer_size", 1)).isConvertibleTo(Json::intValue))
		setVideoBackBufferSize(value.asInt());
	else
		setVideoBackBufferSize(1);

	// ===========================================
	if ((value = json_value.get("video_texture_filter", 0)).isConvertibleTo(Json::intValue))
		setVideoTextureFilter(value.asInt());
	else
		setVideoTextureFilter(0);

	// ===========================================
	if ((value = json_value.get("video_texture_scale_mode", SDL_ScaleModeNearest)).isConvertibleTo(Json::intValue))
		setVideoTextureScaleMode(value.asInt());
	else
		setVideoTextureScaleMode(SDL_ScaleModeNearest);

	// ===========================================
	if ((value = json_value.get("screenshot_path", "")).isConvertibleTo(Json::stringValue))
		setScreenshotPath(value.asCString());
	else
		setScreenshotPath("");

	// ===========================================
	if ((value = json_value.get("enable_debug_console", false)).isConvertibleTo(Json::booleanValue))
		setDebugConsoleEnabled(value.asBool());
	else
		setDebugConsoleEnabled(false);

	// ===========================================
	if ((value = json_value.get("disable_screen_saver", true)).isConvertibleTo(Json::booleanValue))
		setDisableScreenSaver(value.asBool());
	else
		setDisableScreenSaver(true);


#if 0
	// ===========================================
	if ((value = json_value.get("show_lcd_segments", true)).isConvertibleTo(Json::booleanValue))
		setShowLCDSegments(value.asBool());
	else
		setShowLCDSegments(true);

	// ===========================================
	if ((value = json_value.get("audio_enabled", true)).isConvertibleTo(Json::booleanValue))
		setAudioEnabled(value.asBool());
	else
		setAudioEnabled(true);

	// ===========================================
	if ((value = json_value.get("usb_device_id", 0)).isConvertibleTo(Json::intValue))
		setUSBDeviceID(value.asInt());
	else
		setUSBDeviceID(0);
#endif

	// ===========================================
	// TODO
	window_width_ = 240;
	window_height_ = 144;
	window_pos_x_ = SDL_WINDOWPOS_UNDEFINED;
	window_pos_y_ = SDL_WINDOWPOS_UNDEFINED;

	drop_error_frame_ = false;
	keeps_window_topmost_ = false;
}

void NisetroPreviewSDLSetting::setAudioEnabled(bool value)
{
	setting_value_["audio_enabled"] = audio_enabled_ = value;
}

void NisetroPreviewSDLSetting::setAudioDeviceName(const char *device_name)
{
	if (audio_device_name_)
		SDL_free(audio_device_name_);

	audio_device_name_ = (device_name ? SDL_strdup(device_name) : NULL);
	setting_value_["audio_device_name"] = (audio_device_name_ ? audio_device_name_ : Json::Value::nullRef);
}

void NisetroPreviewSDLSetting::setAudioInputFrequency(double frequency)
{
	setting_value_["audio_input_frequency"] = audio_input_frequency_ = frequency;
}

void NisetroPreviewSDLSetting::setAudioResampleQuality(int quality)
{
	if (quality < SRC_SINC_BEST_QUALITY)
		quality = SRC_SINC_BEST_QUALITY;

	if (quality > SRC_LINEAR)
		quality = SRC_LINEAR;

	setting_value_["audio_resample_quality"] = audio_resample_quality_ = quality;
}

void NisetroPreviewSDLSetting::setAudioBufferSize(int buffer_size)
{
	setting_value_["audio_buffer_size"] = audio_buffer_size_ = buffer_size;
}

void NisetroPreviewSDLSetting::setAudioBufferCount(int buffer_count)
{
	setting_value_["audio_buffer_count"] = audio_buffer_count_ = buffer_count;
}

void NisetroPreviewSDLSetting::setAudioInterpolationEnabled(bool value)
{
	setting_value_["audio_interpolation_enabled"] = audio_interpolation_enabled_ = value;
}

void NisetroPreviewSDLSetting::setAudioVolume(float volume)
{
	if (volume < -1.0f)
		volume = -1.0f;

	if (volume > 1.0f)
		volume = 1.0f;

	setting_value_["audio_volume"] = audio_volume_ = volume;
}

void NisetroPreviewSDLSetting::setVideoBackBufferSize(int size)
{
	if (size <= 0)
		size = 1;

	if (size > 5)
		size = 5;

	setting_value_["video_backbuffer_size"] = video_backbuffer_size_ = size;
}

void NisetroPreviewSDLSetting::setVideoTextureFilter(int filter)
{
	if (filter < 0)
		filter = 0;

	if (filter > 2)
		filter = 2;

	setting_value_["video_texture_filter"] = video_texture_filter_ = filter;
}

void NisetroPreviewSDLSetting::setVideoTextureScaleMode(int mode)
{
	if (mode < SDL_ScaleModeNearest)
		mode = SDL_ScaleModeNearest;

	if (mode > SDL_ScaleModeBest)
		mode = SDL_ScaleModeBest;

	setting_value_["video_texture_scale_mode"] = video_texture_scale_mode_ = mode;
}

void NisetroPreviewSDLSetting::setShowLCDSegments(bool value)
{
	setting_value_["show_lcd_segments"] = show_lcd_segments_ = value;
}

void NisetroPreviewSDLSetting::setScreenshotPath(const char *path)
{
	if (screenshot_path_)
		SDL_free(screenshot_path_);

	screenshot_path_ = SDL_strdup((path ? path : ""));
	setting_value_["screenshot_path"] = screenshot_path_;
}

void NisetroPreviewSDLSetting::setUSBDeviceID(uint8_t usb_device_id)
{
	if (usb_device_id > 9)
		usb_device_id = 9;

	setting_value_["usb_device_id"] = usb_device_id_ = usb_device_id;
}

void NisetroPreviewSDLSetting::setDebugConsoleEnabled(bool value)
{
	// read only setting
	debug_console_enabled_ = value;
}

void NisetroPreviewSDLSetting::setDisableScreenSaver(bool value)
{
	setting_value_["disable_screen_saver"] = disable_screen_saver_ = value;
}
