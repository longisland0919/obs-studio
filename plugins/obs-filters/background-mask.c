#include <obs-module.h>
#include "c_api.h"
#include <media-io/video-scaler.h>

#define TFLITE_WIDTH  256
#define TFLITE_HEIGHT 256
#define MAX(x, y) (((x) > (y)) ? (x) : (y))

struct background_mask_filter_data {
	obs_source_t *context;
	gs_effect_t *effect;

	uint8_t * clip_frame;
	uint32_t clip_frame_width;
	uint32_t clip_frame_height;
	uint32_t clip_frame_linesize;
	uint8_t * rgb_int;
	uint32_t rgb_linesize;
	float * rgb_f;
	float * output_probability;
	TfLiteTensor *input_tensor;
	TfLiteInterpreter *interpreter;
	video_scaler_t *scalerToBGR;

	uint8_t *texturedata;
	uint32_t texturedata_linesize;
	gs_texture_t *tex;
	gs_eparam_t *mask;
	gs_eparam_t *texelSize_param;
	gs_eparam_t *step_param;
	gs_eparam_t *radius_param;
	gs_eparam_t *offset_param;
	gs_eparam_t *sigmaTexel_param;
	gs_eparam_t *sigmaColor_param;

	struct vec2* texelSize;
	float step;
	float radius;
	float offset;
	float sigmaTexel;
	float sigmaColor;

	double mask_value;
};

static const char *background_mask_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return "background_mask";
}

static void destroyScalers(struct background_mask_filter_data *filter) {
	if (filter->scalerToBGR) {
		video_scaler_destroy(filter->scalerToBGR);
		filter->scalerToBGR = NULL;
	}
}

static void initializeScalers(
	uint32_t width,
	uint32_t height,
	enum video_format frameFormat,
	struct background_mask_filter_data *filter) {

	struct video_scale_info dst = {
		VIDEO_FORMAT_BGR3,
		TFLITE_WIDTH,
		TFLITE_HEIGHT,
		VIDEO_RANGE_DEFAULT,
		VIDEO_CS_DEFAULT
	};

	struct video_scale_info src = {
		frameFormat,
		width,
		height,
		VIDEO_RANGE_DEFAULT,
		VIDEO_CS_DEFAULT
	};

	destroyScalers(filter);
	video_scaler_create(&filter->scalerToBGR, &dst, &src, VIDEO_SCALE_DEFAULT);
	//	video_scaler_create(&filter->scalerFromBGR, &src, &dst, VIDEO_SCALE_DEFAULT);
}

static void convertFrameToBGR(
	struct obs_source_frame *frame,
	struct background_mask_filter_data *filter) {
	if (filter->scalerToBGR == NULL) {
		// Lazy initialize the frame scale & color converter
		initializeScalers(filter->clip_frame_width, filter->clip_frame_height, frame->format, filter);
	}

	video_scaler_scale(filter->scalerToBGR,
			   &(filter->rgb_int), &(filter->rgb_linesize),
			   &(filter->clip_frame), &(filter->clip_frame_linesize));
}

static void background_mask_destroy(void *data)
{
	struct background_mask_filter_data *filter = data;

	if (filter->effect || filter->tex) {
		obs_enter_graphics();
		if (filter->effect)
			gs_effect_destroy(filter->effect);
		if (filter->tex)
			gs_texture_destroy(filter->tex);
		obs_leave_graphics();
		filter->tex = NULL;
		filter->effect = NULL;
	}

	if (filter->clip_frame) {
		bfree(filter->clip_frame);
		filter->clip_frame = NULL;
	}

	if (filter->rgb_int) {
		bfree(filter->rgb_int);
		filter->rgb_int = NULL;
	}
	if (filter->rgb_f) {
		bfree(filter->rgb_f);
		filter->rgb_f = NULL;
	}
	if (filter->output_probability) {
		bfree(filter->output_probability);
		filter->output_probability = NULL;
	}
	destroyScalers(filter);
	if (filter->interpreter) {
		TfLiteInterpreterDelete(filter->interpreter);
		filter->interpreter = NULL;
	}
	if (filter->input_tensor) {
		filter->input_tensor = NULL;
	}

	if (filter->texturedata) {
		bfree(filter->texturedata);
		filter->texturedata = NULL;
	}

	if (filter->texelSize) {
		bfree(filter->texelSize);
		filter->texelSize = NULL;
	}

	bfree(data);
}

static void *background_mask_create(obs_data_t *settings, obs_source_t *context)
{
	struct background_mask_filter_data *filter =
		bzalloc(sizeof(struct background_mask_filter_data));
	char *effect_path = obs_module_file("background_mask.effect");
	filter->context = context;
	obs_enter_graphics();
	filter->effect = gs_effect_create_from_file(effect_path, NULL);
	filter->mask = gs_effect_get_param_by_name(filter->effect, "mask");
	filter->texelSize_param = gs_effect_get_param_by_name(filter->effect, "u_texelSize");
	filter->step_param = gs_effect_get_param_by_name(filter->effect, "u_step");
	filter->radius_param = gs_effect_get_param_by_name(filter->effect, "u_radius");
	filter->offset_param = gs_effect_get_param_by_name(filter->effect, "u_offset");
	filter->sigmaTexel_param = gs_effect_get_param_by_name(filter->effect, "u_sigmaTexel");
	filter->sigmaColor_param = gs_effect_get_param_by_name(filter->effect, "u_sigmaColor");
	filter->texturedata = NULL;
	obs_leave_graphics();

	bfree(effect_path);

	filter->mask_value = obs_data_get_double(settings, "SETTING_MASK");
	if (!filter->effect) {
		background_mask_destroy(filter);
		return NULL;
	}
	char *model_path = obs_module_file("tflite/mlkit.tflite");
	TfLiteModel *model =
		TfLiteModelCreateFromFile(model_path);
	TfLiteInterpreterOptions *options = TfLiteInterpreterOptionsCreate();
	TfLiteInterpreterOptionsSetNumThreads(options, 2);
	filter->interpreter = TfLiteInterpreterCreate(model, options);
	// The options/model can be deleted immediately after interpreter creation.
	TfLiteInterpreterOptionsDelete(options);
	TfLiteModelDelete(model);
	bfree(model_path);

	TfLiteInterpreterAllocateTensors(filter->interpreter);

	filter->input_tensor = TfLiteInterpreterGetInputTensor(filter->interpreter, 0);
	filter->rgb_linesize = TFLITE_WIDTH * 3;

	return filter;
}

static void background_mask_render(void *data, gs_effect_t *effect)
{
	struct background_mask_filter_data *filter = data;

	obs_source_t *target = obs_filter_get_target(filter->context);
	if (!filter->effect || !target || !filter->texturedata_linesize) {
		blog(LOG_WARNING, "background_mask_render failed ");
		return;
	}
	if (!obs_source_process_filter_begin(filter->context, GS_RGBA,
					     OBS_ALLOW_DIRECT_RENDERING))
		return;
	if (!filter->tex) {
		filter->tex = gs_texture_create(filter->texturedata_linesize, TFLITE_HEIGHT, GS_R8, 1, NULL, GS_DYNAMIC );
	}
	gs_texture_set_image(filter->tex,
			     filter->texturedata,
			     filter->texturedata_linesize, false);
	gs_effect_set_texture(filter->mask,  filter->tex);
	gs_effect_set_vec2(filter->texelSize_param, filter->texelSize);
	gs_effect_set_float(filter->step_param, filter->step);
	gs_effect_set_float(filter->radius_param, filter->radius);
	gs_effect_set_float(filter->offset_param, filter->offset);
	gs_effect_set_float(filter->sigmaTexel_param, filter->sigmaTexel);
	gs_effect_set_float(filter->sigmaColor_param, filter->sigmaColor);

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);

	obs_source_process_filter_end(filter->context, filter->effect, 0, 0);

	gs_blend_state_pop();

	UNUSED_PARAMETER(effect);
}

static void calcSmoothParameters(struct background_mask_filter_data *filter, float frameWidth, float frameHeight, float tfWidth, float tfHeight){
	float sigmaSpace = 1.0f;
	float kSparsityFactor = 0.66f; // Higher is more sparse.

	sigmaSpace *= MAX( frameWidth / tfWidth, frameHeight / tfHeight );

	float sparsity = MAX(1., sqrt(sigmaSpace) * kSparsityFactor);

	filter->step = sparsity;
	filter->radius = sigmaSpace;
	filter->offset = filter->step > 1.f ? filter->step * 0.5f : 0.f;

	float texelWidth = 1.f / frameWidth;
	float texelHeight = 1.f / frameHeight;
	filter->texelSize->x = texelWidth;
	filter->texelSize->y = texelHeight;

	filter->sigmaTexel = MAX(texelWidth, texelHeight) * sigmaSpace;
	filter->sigmaColor = 0.1f;
}

static void mirror_inversion_rgb(uint32_t width, uint32_t height, uint32_t lineSize, uint8_t * data);
static void tflite_get_out(struct background_mask_filter_data * filter);
static void clip_frame(struct obs_source_frame *src_frame,
		       struct background_mask_filter_data *filter);
static bool init_filter_data(struct obs_source_frame *src_frame,
			     struct background_mask_filter_data *filter);

static void convertFrameToRGB(struct obs_source_frame *frame,
			      struct background_mask_filter_data *filter);
static struct obs_source_frame *
background_mask_video(void *data, struct obs_source_frame *frame)
{
	struct background_mask_filter_data *filter = data;
	if (!frame->width || !frame->height) return frame;

	if (!init_filter_data(frame, filter)) {
		return frame;
	}
	clip_frame(frame, filter);
	convertFrameToRGB(frame, filter);
	tflite_get_out(filter);
	if (filter->clip_frame_height == frame->height) {
		int bias = (filter->texturedata_linesize - TFLITE_WIDTH) / 2;
		for (int i = 0; i < TFLITE_HEIGHT; ++i) {
			int p_pos = i * TFLITE_WIDTH;
			int q_pos = i * filter->texturedata_linesize + bias;

			for (int j = 0; j < TFLITE_WIDTH; ++j) {
				if (filter->output_probability[p_pos + j] < filter->mask_value) {
					*(filter->texturedata + q_pos + j) = 0;
				} else {
					*(filter->texturedata + q_pos + j) = (uint8_t) (255.0f * filter->output_probability[p_pos + j]);
				}
			}
		}
	} else {
		//todo
	}
	return frame;
}
static void convertFrameToRGB(struct obs_source_frame *frame,
			      struct background_mask_filter_data *filter)
{
	convertFrameToBGR(frame, filter);
	uint32_t pos;
	uint32_t pos_f;
	for (int i = 0; i < TFLITE_HEIGHT; ++i) {
		for (int j = 0; j <= TFLITE_WIDTH / 2; ++j) {
			pos = filter->rgb_linesize * i + 3 * j;
			pos_f = filter->rgb_linesize * i + 3 * j;
			*(filter->rgb_f + pos_f + 2) = *(filter->rgb_int + pos) / 255.0f;
			*(filter->rgb_f + pos_f + 1) = *(filter->rgb_int + pos + 1) / 255.0f;
			*(filter->rgb_f + pos_f) = *(filter->rgb_int + pos + 2) / 255.0f;
		}
	}
	//mirror bgr image and  make format bgr -> rgb
	mirror_inversion_rgb(TFLITE_WIDTH, TFLITE_HEIGHT, filter->rgb_linesize,
			     filter->rgb_int);
	for (int i = 0; i < TFLITE_HEIGHT; ++i) {
		for (int j = 0; j <= TFLITE_WIDTH / 2; ++j) {
			pos = filter->rgb_linesize * i + 3 * j;
			pos_f = filter->rgb_linesize * i + 3 * (TFLITE_WIDTH - 1 - j);
			*(filter->rgb_f + pos_f) = *(filter->rgb_int + pos) / 255.0f;
			*(filter->rgb_f + pos_f + 1) = *(filter->rgb_int + pos + 1) / 255.0f;
			*(filter->rgb_f + pos_f + 2) = *(filter->rgb_int + pos + 2) / 255.0f;
		}
	}
}
static bool init_filter_data(struct obs_source_frame *src_frame,
			     struct background_mask_filter_data *filter)
{
	if (!src_frame->width || !src_frame->height)
	{
		blog(LOG_WARNING, "init_filter_data failed ,because src frame is invalid");
		return false;
	}
	float default_width_pro = 4;
	float default_height_pro = 3;
	uint32_t clip_width, clip_height;
	if (default_width_pro / default_height_pro >= src_frame->width * 1.f / src_frame->height) {
		//h > w
		clip_width = src_frame->width;
		clip_height = (uint32_t) ((float ) clip_width * default_height_pro / default_width_pro);
		clip_height = clip_height / 2 * 2;
		filter->clip_frame_linesize = src_frame->linesize[0];
		filter->texturedata_linesize = TFLITE_WIDTH;
	} else {
		//w > h
		clip_height = src_frame->height;
		clip_width = (uint32_t) ((float ) clip_height * default_width_pro / default_height_pro);
		clip_width = clip_width / 2 * 2;
		filter->clip_frame_linesize = src_frame->linesize[0] * clip_width / src_frame->width;
		filter->texturedata_linesize = TFLITE_WIDTH * sizeof (uint8_t) * src_frame->width / clip_width;
	}
	if (!filter->texturedata_linesize)
	{
		blog(LOG_WARNING, "cal error texturedata linesize");
		return false;
	}
	filter->texturedata_linesize = (filter->texturedata_linesize + 1) / 2 * 2;
	if (filter->clip_frame_width != clip_width || filter->clip_frame_height != clip_height) {
		destroyScalers(filter);
		filter->clip_frame_width = clip_width;
		filter->clip_frame_height = clip_height;
		if (!filter->texelSize) {
			filter->texelSize = bzalloc(sizeof (struct vec2));
		}
		calcSmoothParameters(filter, src_frame->width, src_frame->height, filter->texturedata_linesize, TFLITE_HEIGHT);
		if (filter->clip_frame) {
			bfree(filter->clip_frame);
		}
		if (filter->texturedata) {
			bfree(filter->texturedata);
		}
	}
	if (!filter->texelSize) {
		filter->texelSize = bzalloc(sizeof (struct vec2));
		calcSmoothParameters(filter, src_frame->width, src_frame->height, filter->texturedata_linesize, TFLITE_HEIGHT);
		if (filter->clip_frame) {
			bfree(filter->clip_frame);
		}
		if (filter->texturedata) {
			bfree(filter->texturedata);
		}
	}
	if (!filter->clip_frame) {
		filter->clip_frame = bzalloc(filter->clip_frame_linesize * filter->clip_frame_height * sizeof (uint8_t));
	}
	if (!filter->texturedata) {
		uint32_t size ;
		if (filter->clip_frame_height == src_frame->height) {
			//w > h
			size = TFLITE_HEIGHT * filter->texturedata_linesize;
		} else {
			// w < h
			size = TFLITE_HEIGHT * filter->texturedata_linesize * sizeof (uint8_t) * src_frame->height / filter->clip_frame_height;
		}
		filter->texturedata = bzalloc(size);
		memset(filter->texturedata, 0, size);
	}
	if (!filter->rgb_int) {
		filter->rgb_int = (
			bzalloc(filter->rgb_linesize * TFLITE_HEIGHT *
				sizeof(uint8_t)));
	}
	if (!filter->rgb_f) {
		filter->rgb_f = bzalloc(filter->rgb_linesize * TFLITE_HEIGHT * sizeof(float));
	}
	return true;
}

static void clip_frame(struct obs_source_frame *src_frame,
		       struct background_mask_filter_data *filter)
{
	uint8_t * src_pos;
	uint8_t * dst_pos;
	if (filter->clip_frame_height == src_frame->height) {
		for (uint32_t i = 0; i < src_frame->height; ++i) {
			src_pos = src_frame->data[0] + i * src_frame->linesize[0] + (src_frame->linesize[0] - filter->clip_frame_linesize) / 2;
			dst_pos = filter->clip_frame + i * filter->clip_frame_linesize;
			memcpy(dst_pos, src_pos, filter->clip_frame_linesize);
		}
	} else if (filter->clip_frame_width == src_frame->width) {

	}
}
static void tflite_get_out(struct background_mask_filter_data * filter) {
	TfLiteTensorCopyFromBuffer(filter->input_tensor, filter->rgb_f,
				   TFLITE_HEIGHT * filter->rgb_linesize * sizeof(float));

	TfLiteInterpreterInvoke(filter->interpreter);

	const TfLiteTensor *output_tensor =
		TfLiteInterpreterGetOutputTensor(filter->interpreter, 0);
	if (!filter->output_probability) {
		filter->output_probability = (
			bzalloc(TFLITE_HEIGHT * TFLITE_WIDTH * sizeof(float)));
	}
	TfLiteTensorCopyToBuffer(output_tensor, filter->output_probability,
				 TFLITE_HEIGHT * TFLITE_WIDTH * sizeof(float));
}

static void mirror_inversion_rgb(uint32_t width, uint32_t height, uint32_t lineSize, uint8_t * data)
{
	if (!data) {
		return;
	}
	//	uint32_t line_size;
	uint8_t tmp;
	for (int i = 0; i < height; ++i) {
		for (int j = 0; j < lineSize / 2; ++j) {
			tmp = data[i * lineSize + j];
			data[i * lineSize + j] = data[i * lineSize + lineSize - 1 - j];
			data[i * lineSize + lineSize - 1 - j] = tmp;
		}
	}

}

static void background_mask_update(void *data, obs_data_t *settings)
{
	struct background_mask_filter_data *filter = data;
	filter->mask_value = obs_data_get_double(settings, "SETTING_MASK");
}

static obs_properties_t *background_mask_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();
	obs_properties_add_float_slider(props, "SETTING_MASK", "MASK_VALUE",
					0.0, 1.0, 0.0001);
	UNUSED_PARAMETER(data);
	return props;
}

static void background_mask_defaults(obs_data_t *settings)
{
	obs_data_set_default_double(settings, "SETTING_MASK", 0.8);
}

struct obs_source_info background_mask_filter = {
	.id = "background-mask-filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_ASYNC,
	.get_name = background_mask_name,
	.create = background_mask_create,
	.destroy = background_mask_destroy,
	.filter_video = background_mask_video,
	.video_render = background_mask_render,
	.update = background_mask_update,
	.get_properties = background_mask_properties,
	.get_defaults = background_mask_defaults,
};
