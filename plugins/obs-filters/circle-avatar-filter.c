#include <obs-module.h>
#ifdef WIN32
#include "background-matting/win/include/c_api.h"
#elif __APPLE__
#include "background-matting/apple/include/c_api.h"
#endif
#include <media-io/video-scaler.h>

#define TFLITE_WIDTH  128
#define TFLITE_HEIGHT 128
//ANCHORS_WIDTH是由计算得到的，这里简化处理，直接赋值
#define ANCHORS_WIDTH 896
#define ANCHORS_HEIGHT 2
#define TFLITE_COORDINATES_NUM 16
#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define HUMAN_THREHOLD 0
#define RELIEVE_SHAKE_BOX_NUM 3
#define RELIEVE_SHAKE_POS_BIAS 0.01
#define RELIEVE_SHAKE_SIZE_BIAS 0.05
#define RELIEVE_SHAKE_POS_SCALE 6
#define RELIEVE_SHAKE_SIZE_SCALE 15

struct box {
	float face_center_x;
	float face_center_y;
	float face_width;
	float face_height;
	struct box *next;
};

struct circle_avatar_filter_data {
	obs_source_t *context;
	gs_effect_t *effect;

	uint8_t * rgb_int;
	uint8_t * clip_frame;
	uint32_t clip_frame_width;
	uint32_t clip_frame_height;
	uint32_t clip_frame_linesize;
	float clip_width_pro;//  clip frame width / source frame width
	float * rgb_f;
	float * output_coordinates_data;
	float * output_score_data;
	float ** anchors;
	TfLiteTensor *input_tensor;
	TfLiteInterpreter *interpreter;
	uint32_t rgb_linesize;
	video_scaler_t *scalerToBGR;
	struct box * box_list;
	struct box current_box;

	gs_eparam_t *face_center_param;
	gs_eparam_t *face_size_param;
	struct vec2* faceCenter;
	struct vec2* faceSize;

	double face_size_scale;
	double x_bias;
	double y_bias;

};
static void tflite_get_out(struct circle_avatar_filter_data * filter);
static void calcRect(struct circle_avatar_filter_data *filter);
static inline void set_box_value(struct box* p_box, float width, float height, float center_x, float center_y);
static bool judgeInBoundary(struct box *p_box);
static void clip_frame(struct obs_source_frame *src_frame,
		       struct circle_avatar_filter_data *filter);
static void init_filter_data(struct obs_source_frame *src_frame,
			     struct circle_avatar_filter_data *filter);
static const char *circle_avatar_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return "circle_avatar";
}

static void destroyScalers(struct circle_avatar_filter_data *filter) {
	if (filter->scalerToBGR) {
		video_scaler_destroy(filter->scalerToBGR);
		filter->scalerToBGR = NULL;
	}
}

static void generateAnchors(struct circle_avatar_filter_data* filter) {
	const int strides[] = {8, 16};
	const int anchors[] = {2, 6};
	if (!filter->anchors) {
		filter->anchors = bzalloc(ANCHORS_WIDTH * sizeof(float *));
		for (int i = 0; i < ANCHORS_WIDTH; ++i) {
			filter->anchors[i] = bzalloc(ANCHORS_HEIGHT * sizeof (float));
		}
	}
	int pos = 0;
	for (unsigned int i = 0; i < sizeof (strides) / sizeof (int); ++i) {
		int stride = strides[i];
		int gridRows = (TFLITE_HEIGHT + stride - 1) / stride;
		int gridCols = (TFLITE_WIDTH + stride - 1) / stride;
		int anchorsNum = anchors[i];
		for (int gridY = 0; gridY < gridRows; ++gridY) {
			float anchorY = stride * (gridY + 0.5f);
			for (int gridX = 0; gridX < gridCols; ++gridX) {
				float anchorX = stride * (gridX + 0.5);
				for (int n = 0; n < anchorsNum; ++n) {
					filter->anchors[pos][0] = anchorX;
					filter->anchors[pos][1] = anchorY;
					pos++;
				}
			}
		}
	}
}

static void initializeScalers(
	uint32_t width,
	uint32_t height,
	enum video_format frameFormat,
	struct circle_avatar_filter_data *filter) {

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
}

static void convertFrameToBGR(
	struct obs_source_frame *frame,
	struct circle_avatar_filter_data *filter) {
	if (filter->scalerToBGR == NULL) {
		// Lazy initialize the frame scale & color converter
		initializeScalers(filter->clip_frame_width, filter->clip_frame_height, frame->format, filter);
	}

	video_scaler_scale(filter->scalerToBGR,
			   &(filter->rgb_int), &(filter->rgb_linesize),
			   &(filter->clip_frame), &(filter->clip_frame_linesize));
}

static void circle_avatar_destroy(void *data)
{
	struct circle_avatar_filter_data *filter = data;

	if (filter->effect) {
		obs_enter_graphics();
		gs_effect_destroy(filter->effect);
		obs_leave_graphics();
	}
	if (filter->rgb_int) {
		bfree(filter->rgb_int);
		filter->rgb_int = NULL;
	}
	if (filter->clip_frame) {
		bfree(filter->clip_frame);
		filter->clip_frame = NULL;
	}
	if (filter->rgb_f) {
		bfree(filter->rgb_f);
		filter->rgb_f = NULL;
	}
	if (filter->output_coordinates_data) {
		bfree(filter->output_coordinates_data);
		filter->output_coordinates_data = NULL;
	}
	if (filter->output_score_data) {
		bfree(filter->output_score_data);
		filter->output_score_data = NULL;
	}
	destroyScalers(filter);
	if (filter->interpreter) {
		TfLiteInterpreterDelete(filter->interpreter);
		filter->interpreter = NULL;
	}
	if (filter->input_tensor) {
		filter->input_tensor = NULL;
	}
	if (filter->anchors) {
		for (int i = 0; i < ANCHORS_WIDTH; ++i) {
			if (filter->anchors[i]) {
				bfree(filter->anchors[i]);
				filter->anchors[i] = NULL;
			}
		}
		bfree(filter->anchors);
		filter->anchors = NULL;
	}
	if (filter->faceSize) {
		bfree(filter->faceSize);
		filter->faceSize = NULL;
	}
	if (filter->faceCenter) {
		bfree(filter->faceCenter);
		filter->faceCenter = NULL;
	}
	if (filter->box_list) {
		struct box * p;
		for (int i = 0; i < RELIEVE_SHAKE_BOX_NUM; ++i) {
			p = filter->box_list;
			filter->box_list = filter->box_list->next;
			bfree(p);
		}
	}

	bfree(data);
}

static void *circle_avatar_create(obs_data_t *settings, obs_source_t *context)
{
	struct circle_avatar_filter_data *filter =
		bzalloc(sizeof(struct circle_avatar_filter_data));
	char *effect_path = obs_module_file("circle_avatar.effect");
	filter->context = context;
	obs_enter_graphics();
	filter->effect = gs_effect_create_from_file(effect_path, NULL);
	filter->face_center_param = gs_effect_get_param_by_name(filter->effect, "u_face_center");
	filter->face_size_param = gs_effect_get_param_by_name(filter->effect, "u_face_size");
	obs_leave_graphics();
	bfree(effect_path);
	if (!filter->effect) {
		circle_avatar_destroy(filter);
		return NULL;
	}

	filter->face_size_scale = obs_data_get_double(settings, "FACE_SCALE_SIZE");
	filter->x_bias = obs_data_get_double(settings, "FACE_X_BIAS");
	filter->y_bias = obs_data_get_double(settings, "FACE_Y_BIAS");

	char *model_path = obs_module_file("tflite/face_detection_front.tflite");
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
	set_box_value(&filter->current_box, -1, -1, -1, -1);

	return filter;
}

static void circle_avatar_render(void *data, gs_effect_t *effect)
{
	struct circle_avatar_filter_data *filter = data;

	if (!obs_source_process_filter_begin(filter->context, GS_RGBA,
					     OBS_ALLOW_DIRECT_RENDERING))
		return;
	gs_effect_set_vec2(filter->face_center_param, filter->faceCenter);
	gs_effect_set_vec2(filter->face_size_param, filter->faceSize);
	//	blog(LOG_ERROR, "song --- face center x = %f, y = %f,  facesize w = %f, h = %f",  filter->faceCenter->x, filter->faceCenter->y, filter->faceSize->x, filter->faceSize->y);

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);

	obs_source_process_filter_end(filter->context, filter->effect, 0, 0);

	gs_blend_state_pop();

	UNUSED_PARAMETER(effect);
}

static struct obs_source_frame *
circle_avatar_video(void *data, struct obs_source_frame *frame)
{
	struct circle_avatar_filter_data *filter = data;
	init_filter_data(frame, filter);
	clip_frame(frame, filter);
	convertFrameToBGR(frame, filter);

	for (uint32_t i = 0; i < TFLITE_HEIGHT * TFLITE_WIDTH; ++i) {
		*(filter->rgb_f + i * 3 + 2) = *(filter->rgb_int + i * 3) / 255.0f;
		*(filter->rgb_f + i * 3 + 1) = *(filter->rgb_int + i * 3 + 1) / 255.0f;
		*(filter->rgb_f + i * 3) = *(filter->rgb_int + i * 3 + 2) / 255.0f;
	}
	if (!filter->anchors) {
		generateAnchors(filter);
	}
	if (!filter->box_list) {
		filter->box_list = bzalloc(sizeof (struct box));
		struct box* head = filter->box_list;
		struct box* next = NULL;
		for (int i = 0; i < RELIEVE_SHAKE_BOX_NUM - 1; ++i) {
			next = bzalloc(sizeof (struct box));
			next->next = NULL;
			next->face_center_x = -1;
			next->face_center_y = -1;
			next->face_width = -1;
			next->face_height = 1;
			head->next = next;
			head = next;
		}
		head->next = filter->box_list;
	}
	tflite_get_out(filter);
	calcRect(filter);

	if (!filter->faceSize) {
		filter->faceSize = bzalloc(sizeof(struct vec2));
	}
	if (!filter->faceCenter) {
		filter->faceCenter = bzalloc(sizeof(struct vec2));
	}
	bool inBox = judgeInBoundary(&filter->current_box);
	if (!inBox) {
		//not in box ,that means face is out of box, and what should we do is keeping location and scale
		return frame;
	} else {
		filter->faceCenter->x = filter->current_box.face_center_x / TFLITE_WIDTH;
		filter->faceCenter->y = filter->current_box.face_center_y / TFLITE_HEIGHT;

		filter->faceSize->x = filter->current_box.face_width * frame->width / TFLITE_WIDTH / filter->clip_frame_width;
		filter->faceSize->y = filter->current_box.face_height * frame->height / TFLITE_HEIGHT / filter->clip_frame_height;
		if (filter->faceCenter->x - filter->faceSize->x / 2 < 0) {
			filter->faceCenter->x = filter->faceSize->x / 2;
		} else if (filter->faceCenter->x + filter->faceSize->x / 2 > 1) {
			filter->faceCenter->x = 1 - filter->faceSize->x / 2;
		}

		if (filter->faceCenter->y - filter->faceSize->y < 0) {
			filter->faceCenter->y = filter->faceSize->y;
		} else if (filter->faceCenter->y + filter->faceSize->y > 1) {
			filter->faceCenter->y = 1 - filter->faceSize->y;
		}
	}

	if (filter->faceCenter->x < 0 || filter->faceCenter->y < 0 || filter->faceSize->x <= 0 || filter->faceSize->y <= 0) {
		filter->faceCenter->x = 0.5f;
		filter->faceCenter->y = 0.5f;
		filter->faceSize->y = (float)(frame->height) / (float) (frame->width);
		filter->faceSize->x = (float)(frame->height) / (float) (frame->width);
	} else {
		filter->faceSize->x = filter->faceSize->x * filter->face_size_scale;
		filter->faceSize->y = filter->faceSize->y * filter->face_size_scale;
	}

	return frame;
}
static void init_filter_data(struct obs_source_frame *src_frame,
			     struct circle_avatar_filter_data *filter)
{
	float default_width_pro = 4;
	float default_height_pro = 3;
	uint32_t clip_width, clip_height;
	if (default_width_pro / default_height_pro >= src_frame->width * 1.f / src_frame->height) {
		//h > w
		clip_width = src_frame->width;
		clip_height = (uint32_t) ((float ) clip_width * default_height_pro / default_width_pro);
		clip_height = clip_height / 2 * 2;
		filter->clip_frame_linesize = src_frame->linesize[0];
	} else {
		//w > h
		clip_height = src_frame->height;
		clip_width = (uint32_t) ((float ) clip_height * default_width_pro / default_height_pro);
		clip_width = clip_width / 2 * 2;
		filter->clip_frame_linesize = src_frame->linesize[0] * clip_width / src_frame->width;
	}

	if (filter->clip_frame_width != clip_width || filter->clip_frame_height != clip_height) {
		destroyScalers(filter);
		filter->clip_frame_width = clip_width;
		filter->clip_frame_height = clip_height;
		if (filter->clip_frame) {
			bfree(filter->clip_frame);
		}
		filter->clip_width_pro = (float) filter->clip_frame_width / (float) src_frame->width;
	}
	if (!filter->clip_frame) {
		filter->clip_frame = bzalloc(filter->clip_frame_linesize * filter->clip_frame_height * sizeof (uint8_t));
	}
	if (!filter->rgb_int) {
		filter->rgb_int = (
			bzalloc(filter->rgb_linesize * TFLITE_HEIGHT *
				sizeof(uint8_t)));
	}
	if (!filter->rgb_f) {
		filter->rgb_f = bzalloc(filter->rgb_linesize * TFLITE_HEIGHT * sizeof(float));
	}
}

static void clip_frame(struct obs_source_frame *src_frame,
		       struct circle_avatar_filter_data *filter)
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
		//todo
	}
}

static inline void set_box_value(struct box* p_box, float width, float height, float center_x, float center_y) {
	p_box->face_center_x = center_x;
	p_box->face_center_y = center_y;
	p_box->face_height = height;
	p_box->face_width = width;
}

static inline void calcRectInner(struct circle_avatar_filter_data *filter, int index) {
	float old_face_center_x = 0;
	float old_face_center_y = 0;
	float old_face_width = 0;
	float old_face_height = 0;
	struct box* p_box = filter->box_list;
	int valid_num = 0;
	for (int i = 0; i < RELIEVE_SHAKE_BOX_NUM; ++i) {
		p_box = p_box->next;
		if (p_box->face_width > 0 && p_box->face_height > 0
		    && p_box->face_center_x > 0 && p_box->face_center_y > 0) {
			old_face_center_x += p_box->face_center_x;
			old_face_center_y += p_box->face_center_y;
			old_face_width += p_box->face_width;
			old_face_height += p_box->face_height;
			valid_num ++;
		}
	}
	//do not find human
	if (index < 0) {
		set_box_value(filter->box_list, -1, -1, -1, -1);
		//has been found human
		if (valid_num > 0) {
			set_box_value(&filter->current_box,
				      old_face_width / valid_num,
				      old_face_height / valid_num,
				      old_face_center_x / valid_num,
				      old_face_center_y / valid_num);
		} else {
			//no human in history
			set_box_value(&filter->current_box, -1, -1, -1, -1);
		}
		return;
	}
	//find human this time
	float current_face_center_x = filter->output_coordinates_data[index * TFLITE_COORDINATES_NUM]
				      + filter->anchors[index][0];
	current_face_center_x = current_face_center_x * filter->clip_width_pro + (1 - filter->clip_width_pro) / 2 * TFLITE_WIDTH + filter->x_bias;
	float current_face_center_y = filter->output_coordinates_data[index * TFLITE_COORDINATES_NUM + 1]
				      + filter->anchors[index][1] + filter->y_bias;
	float current_face_width = filter->output_coordinates_data[index * TFLITE_COORDINATES_NUM + 2];
	current_face_width *= filter->clip_width_pro;
	float current_face_height = filter->output_coordinates_data[index * TFLITE_COORDINATES_NUM + 3];


	set_box_value(filter->box_list, current_face_width, current_face_height, current_face_center_x, current_face_center_y);

	if (valid_num != RELIEVE_SHAKE_BOX_NUM)  {
		set_box_value(&filter->current_box, current_face_width, current_face_height, current_face_center_x, current_face_center_y);
		return;
	}
	if (old_face_center_x || old_face_center_y || old_face_width || old_face_height) {
		old_face_center_x = old_face_center_x / valid_num;
		old_face_center_y = old_face_center_y / valid_num;
		old_face_width = old_face_width / valid_num;
		old_face_height = old_face_height / valid_num;
		float dx = old_face_center_x - filter->current_box.face_center_x;
		float dy = old_face_center_y - filter->current_box.face_center_y;
		float dw = old_face_width - filter->current_box.face_width;
		float dh = old_face_height - filter->current_box.face_height;
		if (dx > TFLITE_WIDTH * RELIEVE_SHAKE_POS_BIAS || dx < -TFLITE_WIDTH * RELIEVE_SHAKE_POS_BIAS) {
			filter->current_box.face_center_x += dx / RELIEVE_SHAKE_POS_SCALE;
		}
		if (dy > TFLITE_HEIGHT * RELIEVE_SHAKE_POS_BIAS || dy < -TFLITE_HEIGHT * RELIEVE_SHAKE_POS_BIAS) {
			filter->current_box.face_center_y += dy / RELIEVE_SHAKE_POS_SCALE;
		}
		if (dw > TFLITE_WIDTH * RELIEVE_SHAKE_SIZE_BIAS || dw < -TFLITE_WIDTH * RELIEVE_SHAKE_SIZE_BIAS) {
			filter->current_box.face_width += dw / RELIEVE_SHAKE_SIZE_SCALE;
		}
		if (dh > TFLITE_HEIGHT * RELIEVE_SHAKE_SIZE_BIAS || dh < -TFLITE_HEIGHT * RELIEVE_SHAKE_SIZE_BIAS) {
			filter->current_box.face_height += dh / RELIEVE_SHAKE_SIZE_SCALE;
		}
	}

}

static bool judgeInBoundary(struct box *p_box) {
	return p_box->face_center_x > 0 &&  p_box->face_center_x < TFLITE_WIDTH && p_box->face_center_y > 0 && p_box->face_center_y < TFLITE_HEIGHT;
}

static void calcRect(struct circle_avatar_filter_data *filter) {
	int pos = -1;
	int best_score = HUMAN_THREHOLD;
	filter->box_list = filter->box_list->next;
	for (int i = 0; i < ANCHORS_WIDTH; ++i) {
		if (filter->output_score_data[i] > best_score) {
			pos = i;
			best_score = filter->output_score_data[i];
		}
	}
	calcRectInner(filter, pos);
}

static void tflite_get_out(struct circle_avatar_filter_data * filter) {
	TfLiteTensorCopyFromBuffer(filter->input_tensor, filter->rgb_f,
				   TFLITE_HEIGHT * filter->rgb_linesize * sizeof(float));

	TfLiteInterpreterInvoke(filter->interpreter);

	const TfLiteTensor *output_tensor =
		TfLiteInterpreterGetOutputTensor(filter->interpreter, 0);
	if (!filter->output_coordinates_data) {
		filter->output_coordinates_data = (
			bzalloc(ANCHORS_WIDTH * TFLITE_COORDINATES_NUM * sizeof(float)));
	}
	TfLiteTensorCopyToBuffer(output_tensor, filter->output_coordinates_data,
				 ANCHORS_WIDTH * TFLITE_COORDINATES_NUM * sizeof(float));
	output_tensor =
		TfLiteInterpreterGetOutputTensor(filter->interpreter, 1);
	if (!filter->output_score_data) {
		filter->output_score_data = (
			bzalloc(ANCHORS_WIDTH * sizeof (float)));
	}
	TfLiteTensorCopyToBuffer(output_tensor, filter->output_score_data,
				 ANCHORS_WIDTH * sizeof(float));

}

static void circle_avatar_update(void *data, obs_data_t *settings)
{
	struct circle_avatar_filter_data *filter = data;
	filter->x_bias = obs_data_get_double(settings, "FACE_X_BIAS");
	filter->y_bias = obs_data_get_double(settings, "FACE_Y_BIAS");
	filter->face_size_scale = obs_data_get_double(settings, "FACE_SCALE_SIZE");
}

static obs_properties_t *circle_avatar_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();
	obs_properties_add_float_slider(props, "FACE_SCALE_SIZE", "FACE_SCALE_SIZE",
					1, 4, 0.1);
	obs_properties_add_float_slider(props, "FACE_X_BIAS", "FACE_X_BIAS",
					-10, 10, 0.1);
	obs_properties_add_float_slider(props, "FACE_Y_BIAS", "FACE_Y_BIAS",
					-10, 10, 0.1);
	UNUSED_PARAMETER(data);
	return props;
}

static void circle_avatar_defaults(obs_data_t *settings)
{
	obs_data_set_default_double(settings, "FACE_SCALE_SIZE", 2.0);
	obs_data_set_default_double(settings, "FACE_X_BIAS", 0.0);
	obs_data_set_default_double(settings, "FACE_Y_BIAS", 0.0);
}


struct obs_source_info circle_avatar_filter = {
	.id = "circle-avatar-filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_ASYNC,
	.get_name = circle_avatar_name,
	.create = circle_avatar_create,
	.destroy = circle_avatar_destroy,
	.video_render = circle_avatar_render,
	.filter_video = circle_avatar_video,
	.update = circle_avatar_update,
	.get_properties = circle_avatar_properties,
	.get_defaults = circle_avatar_defaults,
};
