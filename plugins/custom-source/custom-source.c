#include <obs-module.h>

#include <util/dstr.h>
#include <util/threading.h>
//
#define blog(log_level, format, ...)                    \
	blog(log_level, "[custom_source: '%s'] " format, \
	     obs_source_get_name(context->source), ##__VA_ARGS__)

#define debug(format, ...) blog(LOG_DEBUG, format, ##__VA_ARGS__)
#define info(format, ...) blog(LOG_INFO, format, ##__VA_ARGS__)
#define warn(format, ...) blog(LOG_WARNING, format, ##__VA_ARGS__)

struct custom_source {
	obs_source_t *source;

	uint32_t width;
	uint32_t height;
	uint8_t * (* start_bitmap_data)(void *);
	void (* end_bitmap_data)(void *);
	void * func_param;
	bool capture_data_async;
	gs_texture_t *bitmap_texture;

	DARRAY(uint8_t) buffer;
	pthread_t data_thread;
	os_event_t *capture_event;
	os_event_t *stop_event;
};


static const char *custom_source_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return "custom_source";
}

static bool update_frame_inner(struct custom_source *context)
{
	if (!context->start_bitmap_data || context->width <= 0 || context->height <= 0) goto fail;
	const uint8_t * data = context->start_bitmap_data(context->func_param);
	if (!data)
	{
		if (context->end_bitmap_data && context->func_param)
			context->end_bitmap_data(context->func_param);
		goto fail;
	}
//	if (!capture_data_async)
//	{
		obs_enter_graphics();
		if (context->bitmap_texture)
		{
			if(context->width != gs_texture_get_width(context->bitmap_texture) || context->height != gs_texture_get_height(context->bitmap_texture))
			{
				gs_texture_destroy(context->bitmap_texture);
				blog(LOG_ERROR, "create width = %d height = %d 22222", context->width, context->height);
				context->bitmap_texture = gs_texture_create(context->width, context->height, GS_RGBA, 1, &data, GS_DYNAMIC);
			} else
			{
				gs_texture_set_image(context->bitmap_texture, data,
						     context->width * 4, false);
			}
		} else
		{
			blog(LOG_ERROR, "create width = %d height = %d", context->width, context->height);
			context->bitmap_texture = gs_texture_create(context->width, context->height, GS_RGBA, 1, &data, GS_DYNAMIC);
		}
		obs_leave_graphics();
//	} else
//	{
//		da_resize(context->buffer, context->width * context->height * 4);
//		uint8_t * frame_data = context->buffer.array;
//		memcpy(frame_data, data, context->width * context->height * 4);
//		uint64_t ts = os_gettime_ns();
//		struct obs_source_frame frame = {
//			.format = VIDEO_FORMAT_RGBA,
//			.width = context->width,
//			.height = context->height,
//			.data[0] = frame_data,
//			.linesize[0] = context->width * 4,
//			.timestamp = ts,
//		};
//
//		obs_source_output_video(context->source, &frame);
//		blog(LOG_ERROR, "update_frame_inner async ts = %llu", ts);
//	}

	if (context->end_bitmap_data && context->func_param)
		context->end_bitmap_data(context->func_param);
	return true;
fail:
	if (!context->bitmap_texture)
	{
		context->width = 0;
		context->height = 0;
	} else
	{
		obs_enter_graphics();
		context->width = gs_texture_get_width(context->bitmap_texture);
		context->height = gs_texture_get_height(context->bitmap_texture);
		obs_leave_graphics();
	}
	return false;
}

void* get_frame_thread(void * data)
{
	struct custom_source *context = data;
	da_init(context->buffer);
	for (;;) {
		os_event_wait(context->capture_event);
		if (os_event_try(context->stop_event) != EAGAIN)
			break;

		update_frame_inner(context);
	}
	da_free(context->buffer);
	return NULL;

}

static bool update_frame(struct custom_source *context, bool capture_data_async)
{
	if (!capture_data_async)
	{
		if (context->capture_data_async)
		{
			blog(LOG_ERROR, "cannot turn to sync, because you have been in async");
			return false;
		}
		update_frame_inner(context);
	} else {
		if (!context->capture_data_async)
		{
			context->capture_data_async = capture_data_async;
			os_event_init(&context->capture_event, OS_EVENT_TYPE_AUTO);
			os_event_init(&context->stop_event, OS_EVENT_TYPE_MANUAL);
			pthread_create(&context->data_thread, NULL, get_frame_thread, context);
		}
	}
	context->capture_data_async = capture_data_async;
	return true;
}

static void custom_source_update(void *data, obs_data_t *settings)
{
	struct custom_source *context = data;
	context->width = obs_data_get_int(settings, "width");
	context->height = obs_data_get_int(settings, "height");
	long long callbackHandle = obs_data_get_int(settings, "frame_start_callback");
	long long paramHandle = obs_data_get_int(settings, "callback_param");
	if (callbackHandle != 0 && paramHandle != 0)
	{
		context->start_bitmap_data = (uint8_t *(*)(void *))callbackHandle;
		context->func_param = (void *)paramHandle;
	}
	callbackHandle = obs_data_get_int(settings, "frame_end_callback");
	if (callbackHandle != 0)
		context->end_bitmap_data = (void (*)(void *))callbackHandle;
	bool capture_data_async = obs_data_get_bool(settings, "capture_bitmap_positive");
	update_frame(context, capture_data_async);
}

static void custom_capture_tick(void *data, float seconds)
{
	UNUSED_PARAMETER(seconds);
	struct custom_source* context = data;
	if (!obs_source_showing(context->source) || !context->capture_data_async)
		return;
	os_event_signal(context->capture_event);
}

static void custom_source_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "width", 0);
	obs_data_set_default_int(settings, "height", 0);
	obs_data_set_default_int(settings, "frame_start_callback", 0);
	obs_data_set_default_int(settings, "frame_end_callback", 0);
	obs_data_set_default_bool(settings, "capture_bitmap_positive", false);
}

static void *custom_source_create(obs_data_t *settings, obs_source_t *source)
{
	struct custom_source *context = bzalloc(sizeof(struct custom_source));
	context->source = source;
	context->start_bitmap_data = NULL;
	context->capture_data_async = false;
	UNUSED_PARAMETER(settings);
	return context;
}

static void custom_source_destroy(void *data)
{
	struct custom_source *context = data;
	if (context->bitmap_texture)
	{
		obs_enter_graphics();
		gs_texture_destroy(context->bitmap_texture);
		obs_leave_graphics();
	}
	if (context->capture_data_async)
	{
		os_event_signal(context->stop_event);
		os_event_signal(context->capture_event);
		pthread_join(context->data_thread, NULL);
		os_event_destroy(context->capture_event);
		os_event_destroy(context->stop_event);
	}
	bfree(context);
}

static uint32_t custom_source_getwidth(void *data)
{
	struct custom_source *context = data;
	return context->width;
}

static uint32_t custom_source_getheight(void *data)
{
	struct custom_source *context = data;
	return context->height;
}

static void custom_source_render(void *data, gs_effect_t * effect)
{
	struct custom_source *context = data;
	if (context->width <= 0 || context->height <= 0 || !context->bitmap_texture)
		return;

	const bool previous = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(true);

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
	gs_eparam_t *const param = gs_effect_get_param_by_name(effect, "image");
	gs_effect_set_texture_srgb(param, context->bitmap_texture);

	gs_draw_sprite(context->bitmap_texture, 0, context->width, context->height);
	gs_blend_state_pop();
	gs_enable_framebuffer_srgb(previous);
}

static struct obs_source_info custom_source_info = {
	.id = "custom_source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_SRGB,
	.get_name = custom_source_get_name,
	.create = custom_source_create,
	.destroy = custom_source_destroy,
	.update = custom_source_update,
	.get_defaults = custom_source_defaults,
	.get_width = custom_source_getwidth,
	.get_height = custom_source_getheight,
	.video_render = custom_source_render,
	.video_tick = custom_capture_tick,
	.icon_type = OBS_ICON_TYPE_IMAGE,
};

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("custom-source", "en-US")
MODULE_EXPORT const char *obs_module_description(void)
{
	return "custom source, using for programming source";
}

bool obs_module_load(void)
{
	obs_register_source(&custom_source_info);
	return true;
}
