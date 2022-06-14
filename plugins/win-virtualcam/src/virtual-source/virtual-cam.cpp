#include <streams.h>
#include <stdio.h>
#include <olectl.h>
#include <dvdmedia.h>
#include <commctrl.h>
#include "virtual-cam.h"
#include "clock.h"

#define MIN_WIDTH 320
#define MIN_HEIGHT 240
#define MAX_WIDTH 4096
#define MAX_HEIGHT 3072
#define MAX_FRAMETIME 1000000
#define MIN_FRAMETIME 166666
#define SLEEP_DURATION 5

extern bool initialize_placeholder();
extern const uint8_t *get_placeholder_ptr();
extern const bool get_placeholder_size(int *out_cx, int *out_cy);


CUnknown * WINAPI CreateInstance(LPUNKNOWN lpunk, HRESULT *phr)
{
	ASSERT(phr);
	CUnknown *punk = new CVCam(lpunk, phr, CLSID_OBS_VirtualV, ModeVideo);
	return punk;
}

CUnknown * WINAPI CreateInstance2(LPUNKNOWN lpunk, HRESULT *phr)
{
	ASSERT(phr);
	CUnknown *punk = new CVCam(lpunk, phr, CLSID_OBS_VirtualV2, ModeVideo2);
	return punk;
}

CUnknown * WINAPI CreateInstance3(LPUNKNOWN lpunk, HRESULT *phr)
{
	ASSERT(phr);
	CUnknown *punk = new CVCam(lpunk, phr, CLSID_OBS_VirtualV3, ModeVideo3);
	return punk;
}

CUnknown * WINAPI CreateInstance4(LPUNKNOWN lpunk, HRESULT *phr)
{
	ASSERT(phr);
	CUnknown *punk = new CVCam(lpunk, phr, CLSID_OBS_VirtualV4, ModeVideo4);
	return punk;
}

CVCam::CVCam(LPUNKNOWN lpunk, HRESULT *phr, const GUID id, int mode) :
CSource(NAME("OBS Virtual CAM"), lpunk, id)
{
	ASSERT(phr);
	CAutoLock cAutoLock(&m_cStateLock);
	m_paStreams = (CSourceStream **) new CVCamStream*[1];
	stream = new CVCamStream(phr, this, L"Video", mode);
	m_paStreams[0] = stream;
}

HRESULT CVCam::NonDelegatingQueryInterface(REFIID riid, void **ppv)
{
	if (riid == _uuidof(IAMStreamConfig) || riid == _uuidof(IKsPropertySet))
		return m_paStreams[0]->QueryInterface(riid, ppv);
	else
		return CSource::NonDelegatingQueryInterface(riid, ppv);
}

CVCamStream::CVCamStream(HRESULT *phr, CVCam *pParent, LPCWSTR pPinName, int mode) :
CSourceStream(NAME("Video"), phr, pParent, pPinName), parent(pParent)
{
	queue_mode = mode;
	ListSupportFormat();
	use_obs_format_init = CheckObsSetting();
	GetMediaType(0, &m_mt);
	prev_end_ts = 0;
}

CVCamStream::~CVCamStream()
{
	// if (placeholder.scaled_data)
	// 	free(placeholder.scaled_data);
}

bool CVCamStream::CheckObsSetting()
{
	bool get = shared_queue_get_video_format(queue_mode,&obs_format, 
		&obs_width, &obs_height, &obs_frame_time);


	if (get) {
		if (obs_frame_time < MIN_FRAMETIME || obs_frame_time > MAX_FRAMETIME)
			return false;

		if (obs_height < MIN_HEIGHT) {
			obs_width = obs_width * MIN_HEIGHT / obs_height;
			obs_height = MIN_HEIGHT;
		}

		if (obs_width < MIN_WIDTH) {
			obs_height = obs_height * MIN_WIDTH / obs_width;
			obs_width = MIN_WIDTH;
		}

		if (obs_height % 2 != 0)
			obs_height += 1;

		format_list.push_front(struct format(obs_width, obs_height, 
			obs_frame_time));
	}

	return get;
}

void CVCamStream::SetConvertContext()
{
	VIDEOINFOHEADER *pvi = (VIDEOINFOHEADER *)(m_mt.Format());
	scale_info.dst_format = AV_PIX_FMT_YUYV422;
	scale_info.dst_width = pvi->bmiHeader.biWidth;
	scale_info.dst_height = pvi->bmiHeader.biHeight;
	scale_info.dst_linesize[0] = pvi->bmiHeader.biWidth * 2;
}

void CVCamStream::SetSyncTimeout()
{
	if (queue.header) {
		sync_timeout = queue.header->queue_length *
			queue.header->frame_time / 100;
	}
	else {
		sync_timeout = 10 * ((VIDEOINFOHEADER*)m_mt.pbFormat)->AvgTimePerFrame;
	}

}

void CVCamStream::SetGetTimeout()
{
	get_timeout = ((VIDEOINFOHEADER*)m_mt.pbFormat)->AvgTimePerFrame * 3 / (SLEEP_DURATION * 10000);
}

HRESULT CVCamStream::QueryInterface(REFIID riid, void **ppv)
{
	if (riid == _uuidof(IAMStreamConfig))
		*ppv = (IAMStreamConfig*)this;
	else if (riid == _uuidof(IKsPropertySet))
		*ppv = (IKsPropertySet*)this;
	else
		return CSourceStream::QueryInterface(riid, ppv);

	AddRef();
	return S_OK;
}

void doHorrizontalMirror(uint32_t H, uint32_t W,
    uint8_t *pIn)
{
 	unsigned char *pIn_start = NULL;
	unsigned char *pOut_end = NULL;
	int j = 0,k = 0;
	for (int i = 0;i < H;i++) {
		pIn_start = pIn + 2 * i * W;
		pOut_end = pIn + 2 * i * W;
		k= 2 * W;
		j= 0;
		unsigned char tmp;
		for (j = 0; j < k; j += 4) {

			// 4  1  2  3
			// U0 Y0 V0 Y1 
			// Y1 V0 Y0 U0

			// 2  3  4  1
			// Y0 U0 Y1 V0

			tmp = *(pIn_start + j);
			*(pIn_start+j) = *(pIn_start+2*W-2-j);
			*(pIn_start+2*W-2-j)=tmp;
			
			tmp=*(pIn_start+j+1);
			*(pIn_start+j+1)=*(pIn_start+2*W-3-j);
			*(pIn_start+2*W-3-j)=tmp;

			tmp=*(pIn_start+j+2);
			*(pIn_start+j+2)=*(pIn_start+2*W-4-j);
			*(pIn_start+2*W-4-j)=tmp;

			tmp=*(pIn_start+j+3);
			*(pIn_start+j+3)=*(pIn_start+2*W-1-j);
			*(pIn_start+2*W-1-j)=tmp;

			// tmp=*(pIn_start+j);
			// *(pIn_start+j) = *(pIn_start+2*W-4-j);
			// *(pIn_start+2*W-4-j)=tmp;
			
			// tmp=*(pIn_start+j+1);
			// *(pIn_start+j+1)=*(pIn_start+2*W-1-j);
			// *(pIn_start+2*W-1-j)=tmp;

			// tmp=*(pIn_start+j+2);
			// *(pIn_start+j+2)=*(pIn_start+2*W-2-j);
			// *(pIn_start+2*W-2-j)=tmp;

			// tmp=*(pIn_start+j+3);
			// *(pIn_start+j+3)=*(pIn_start+2*W-3-j);
			// *(pIn_start+2*W-3-j)=tmp;

			k=k-4;			
		}
	}
}


HRESULT CVCamStream::FillBuffer(IMediaSample *pms)
{
	HRESULT hr;
	bool get_sample = false;
	int get_times = 0;
	uint64_t timestamp = 0;
	uint64_t current_time = 0;
	REFERENCE_TIME start_time = 0;
	REFERENCE_TIME end_time = 0;
	REFERENCE_TIME duration = 0;

	hr = pms->GetPointer((BYTE**)&dst);

	if (system_start_time <= 0) 
		system_start_time = get_current_time();
	else 
		current_time = get_current_time(system_start_time);
	

	if (!queue.hwnd) {
		if (shared_queue_open(&queue, queue_mode)) {

			shared_queue_set_video_connect_num(&queue, true);
			shared_queue_get_video_format(queue_mode, &format, &frame_width,
				&frame_height, &time_perframe);
			SetConvertContext();
			SetGetTimeout();
			sync_timeout = 0;
		}
	}

	if (sync_timeout <= 0) {
		SetSyncTimeout();
	}
	else if (prev_end_ts >current_time) {
		sleepto(prev_end_ts, system_start_time);
	}
	else if (current_time - prev_end_ts > sync_timeout) {
		if(queue.header) 
			share_queue_init_index(&queue);
		else
			prev_end_ts = current_time;
	}

	while (queue.header && !get_sample) {

		if (get_times >= get_timeout || queue.header->state != OutputReady) {
			get_sample = false;
			break;
		}

		get_sample = shared_queue_get_video(&queue, &scale_info, dst, 
			&timestamp);

		if (!get_sample) {
			Sleep(SLEEP_DURATION);
			get_times++;
		}
	}
	
	if (get_sample && !obs_start_ts) {
		obs_start_ts = timestamp;
		dshow_start_ts = prev_end_ts;
	}

	if (get_sample) {
		if (queue.header->hori_flip == 1) {
			doHorrizontalMirror(scale_info.dst_height, scale_info.dst_width, dst);
		}
		start_time = dshow_start_ts + (timestamp - obs_start_ts) / 100;
		duration = time_perframe;
	} else {
		// int size = pms->GetActualDataLength();
		// memset(dst, 127, size);
		if (cx != GetCX() || cy != GetCY()) {
			cx = GetCX();
			cy = GetCY();
			UpdatePlaceholder();
		}
	  	ShowDefaultFrame(dst, pms);
	   
		start_time = prev_end_ts;
		duration = ((VIDEOINFOHEADER*)m_mt.pbFormat)->AvgTimePerFrame;
	}

	if (queue.header && queue.header->state == OutputStop || get_times > 20) {
		shared_queue_read_close(&queue, &scale_info);
		dshow_start_ts = 0;
		obs_start_ts = 0;
		sync_timeout = 0;
	}

	end_time = start_time + duration;
	prev_end_ts = end_time;
	pms->SetTime(&start_time, &end_time);
	pms->SetSyncPoint(TRUE);

	return NOERROR;
}

STDMETHODIMP CVCamStream::Notify(IBaseFilter * pSender, Quality q)
{
	return E_NOTIMPL;
}

bool CVCamStream::ListSupportFormat()
{
	if (format_list.size() > 0)
		format_list.empty();
	
	format_list.push_back(struct format(1920, 1080, 333333));
	format_list.push_back(struct format(1280, 720, 333333));
	format_list.push_back(struct format(960, 540, 333333));
	format_list.push_back(struct format(640, 360, 333333));

	return true;
}

HRESULT CVCamStream::SetMediaType(const CMediaType *pmt)
{
	DECLARE_PTR(VIDEOINFOHEADER, pvi, pmt->Format());
	HRESULT hr = CSourceStream::SetMediaType(pmt);
	return hr;
}

HRESULT CVCamStream::GetMediaType(int iPosition,CMediaType *pmt)
{
	if (format_list.size() == 0)
		ListSupportFormat();

	if (iPosition < 0 || iPosition > format_list.size()-1)
		return E_INVALIDARG;

	DECLARE_PTR(VIDEOINFOHEADER, pvi, 
	pmt->AllocFormatBuffer(sizeof(VIDEOINFOHEADER)));
	ZeroMemory(pvi, sizeof(VIDEOINFOHEADER));


	pvi->bmiHeader.biWidth = format_list[iPosition].width;
	pvi->bmiHeader.biHeight = format_list[iPosition].height;
	pvi->AvgTimePerFrame = format_list[iPosition].time_per_frame;
	pvi->bmiHeader.biCompression = MAKEFOURCC('Y', 'U', 'Y', '2');
	pvi->bmiHeader.biBitCount = 16;
	pvi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	pvi->bmiHeader.biPlanes = 1;
	pvi->bmiHeader.biSizeImage = pvi->bmiHeader.biWidth * 
		pvi->bmiHeader.biHeight * 2;
	pvi->bmiHeader.biClrImportant = 0;

	SetRectEmpty(&(pvi->rcSource)); 
	SetRectEmpty(&(pvi->rcTarget)); 

	pmt->SetType(&MEDIATYPE_Video);
	pmt->SetFormatType(&FORMAT_VideoInfo);
	pmt->SetTemporalCompression(FALSE);
	pmt->SetSubtype(&MEDIASUBTYPE_YUY2);
	pmt->SetSampleSize(pvi->bmiHeader.biSizeImage);

	// cx = GetCX();
	// cy = GetCY();
	// LoadPlaceholder();
	return NOERROR;
} 

HRESULT CVCamStream::CheckMediaType(const CMediaType *pMediaType)
{
	if (pMediaType == nullptr)
		return E_FAIL;

	VIDEOINFOHEADER *pvi = (VIDEOINFOHEADER *)(pMediaType->Format());

	const GUID* type = pMediaType->Type();
	const GUID* info = pMediaType->FormatType();
	const GUID* subtype = pMediaType->Subtype();

	if (*type != MEDIATYPE_Video)
		return E_INVALIDARG;

	if (*info != FORMAT_VideoInfo)
		return E_INVALIDARG;

	if (*subtype != MEDIASUBTYPE_YUY2)
		return E_INVALIDARG;

	if (pvi->AvgTimePerFrame < 166666 || pvi->AvgTimePerFrame >1000000)
		return E_INVALIDARG;

	if (ValidateResolution(pvi->bmiHeader.biWidth, pvi->bmiHeader.biHeight)) {
		return S_OK;
	}
	return E_INVALIDARG;
} 

bool CVCamStream::ValidateResolution(long width,long height)
{
	if (width < 320 || height < 240)
		return false;
	else if (width > 4096)
		return false;
	else if (width * 9 == height * 16)
		return true;
	else if (width * 3 == height * 4)
		return true;
	else if (use_obs_format_init && width == obs_width && height == obs_height)
		return true;
	else
		return false;
}

HRESULT CVCamStream::DecideBufferSize(IMemAllocator *pAlloc, 
	ALLOCATOR_PROPERTIES *pProperties)
{
	CAutoLock cAutoLock(m_pFilter->pStateLock());
	HRESULT hr = NOERROR;

	VIDEOINFOHEADER *pvi = (VIDEOINFOHEADER *)m_mt.Format();
	pProperties->cBuffers = 1;
	pProperties->cbBuffer = pvi->bmiHeader.biSizeImage;

	ALLOCATOR_PROPERTIES Actual;
	hr = pAlloc->SetProperties(pProperties, &Actual);

	if (FAILED(hr)) return hr;
	if (Actual.cbBuffer < pProperties->cbBuffer) return E_FAIL;

	return NOERROR;
} 

HRESULT CVCamStream::OnThreadCreate()
{
	prev_end_ts = 0;
	obs_start_ts = 0;
	dshow_start_ts = 0;
	system_start_time = 0;

	cx = GetCX();
	cy = GetCY();
	LoadPlaceholder();
	return NOERROR;
}

HRESULT CVCamStream::OnThreadDestroy()
{
	if (queue.header) 
		shared_queue_read_close(&queue, &scale_info);

	return NOERROR;
}

HRESULT STDMETHODCALLTYPE CVCamStream::SetFormat(AM_MEDIA_TYPE *pmt)
{
	if (pmt == nullptr)
		return E_FAIL;

	if (parent->GetState() != State_Stopped)
		return E_FAIL;

	if (CheckMediaType((CMediaType *)pmt) != S_OK)
		return E_FAIL;

	VIDEOINFOHEADER *pvi = (VIDEOINFOHEADER *)(pmt->pbFormat);

	m_mt.SetFormat(m_mt.Format(), sizeof(VIDEOINFOHEADER));
	format_list.push_front(struct format(pvi->bmiHeader.biWidth, 
		pvi->bmiHeader.biHeight, pvi->AvgTimePerFrame));

	IPin* pin;
	ConnectedTo(&pin);
	if (pin){
		IFilterGraph *pGraph = parent->GetGraph();
		pGraph->Reconnect(this);
	}
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CVCamStream::GetFormat(AM_MEDIA_TYPE **ppmt)
{
	*ppmt = CreateMediaType(&m_mt);
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CVCamStream::GetNumberOfCapabilities(int *piCount, 
	int *piSize)
{
	if (format_list.size() == 0)
		ListSupportFormat();
	
	*piCount = format_list.size();
	*piSize = sizeof(VIDEO_STREAM_CONFIG_CAPS);
	return S_OK;
}

HRESULT STDMETHODCALLTYPE CVCamStream::GetStreamCaps(int iIndex, 
	AM_MEDIA_TYPE **pmt, BYTE *pSCC)
{
	if (format_list.size() == 0)
		ListSupportFormat();

	if (iIndex < 0 || iIndex > format_list.size() - 1)
		return E_INVALIDARG;

	*pmt = CreateMediaType(&m_mt);
	DECLARE_PTR(VIDEOINFOHEADER, pvi, (*pmt)->pbFormat);


	pvi->bmiHeader.biWidth = format_list[iIndex].width;
	pvi->bmiHeader.biHeight = format_list[iIndex].height;
	pvi->AvgTimePerFrame = format_list[iIndex].time_per_frame;
	pvi->AvgTimePerFrame = 333333;
	pvi->bmiHeader.biCompression = MAKEFOURCC('Y', 'U', 'Y', '2');
	pvi->bmiHeader.biBitCount = 16;
	pvi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	pvi->bmiHeader.biPlanes = 1;
	pvi->bmiHeader.biSizeImage = pvi->bmiHeader.biWidth * 
		pvi->bmiHeader.biHeight * 2;
	pvi->bmiHeader.biClrImportant = 0;

	SetRectEmpty(&(pvi->rcSource)); 
	SetRectEmpty(&(pvi->rcTarget)); 

	(*pmt)->majortype = MEDIATYPE_Video;
	(*pmt)->subtype = MEDIASUBTYPE_YUY2;
	(*pmt)->formattype = FORMAT_VideoInfo;
	(*pmt)->bTemporalCompression = FALSE;
	(*pmt)->bFixedSizeSamples = FALSE;
	(*pmt)->lSampleSize = pvi->bmiHeader.biSizeImage;
	(*pmt)->cbFormat = sizeof(VIDEOINFOHEADER);

	DECLARE_PTR(VIDEO_STREAM_CONFIG_CAPS, pvscc, pSCC);

	pvscc->guid = FORMAT_VideoInfo;
	pvscc->VideoStandard = AnalogVideo_None;
	pvscc->InputSize.cx = pvi->bmiHeader.biWidth;
	pvscc->InputSize.cy = pvi->bmiHeader.biHeight;
	pvscc->MinCroppingSize.cx = pvi->bmiHeader.biWidth;
	pvscc->MinCroppingSize.cy = pvi->bmiHeader.biHeight;
	pvscc->MaxCroppingSize.cx = pvi->bmiHeader.biWidth;
	pvscc->MaxCroppingSize.cy = pvi->bmiHeader.biHeight;
	pvscc->CropGranularityX = pvi->bmiHeader.biWidth;
	pvscc->CropGranularityY = pvi->bmiHeader.biHeight;
	pvscc->CropAlignX = 0;
	pvscc->CropAlignY = 0;

	pvscc->MinOutputSize.cx = pvi->bmiHeader.biWidth;
	pvscc->MinOutputSize.cy = pvi->bmiHeader.biHeight;
	pvscc->MaxOutputSize.cx = pvi->bmiHeader.biWidth;
	pvscc->MaxOutputSize.cy = pvi->bmiHeader.biHeight;
	pvscc->OutputGranularityX = 0;
	pvscc->OutputGranularityY = 0;
	pvscc->StretchTapsX = 0;
	pvscc->StretchTapsY = 0;
	pvscc->ShrinkTapsX = 0;
	pvscc->ShrinkTapsY = 0;
	pvscc->MinFrameInterval = pvi->AvgTimePerFrame;   
	pvscc->MaxFrameInterval = pvi->AvgTimePerFrame; 
	pvscc->MinBitsPerSecond = pvi->bmiHeader.biWidth * pvi->bmiHeader.biHeight 
		* 2 * 8 * (10000000 / pvi->AvgTimePerFrame);
	pvscc->MaxBitsPerSecond = pvi->bmiHeader.biWidth * pvi->bmiHeader.biHeight 
		* 2 * 8 * (10000000 / pvi->AvgTimePerFrame);

	return S_OK;
}

HRESULT CVCamStream::Set(REFGUID guidPropSet, DWORD dwID, void *pInstanceData,
	DWORD cbInstanceData, void *pPropData, DWORD cbPropData)
{
	return E_NOTIMPL;
}

HRESULT CVCamStream::Get(REFGUID guidPropSet,DWORD dwPropID,
	void *pInstanceData,DWORD cbInstanceData,void *pPropData,
	DWORD cbPropData,DWORD *pcbReturned)
{
	if (guidPropSet != AMPROPSETID_Pin)             return E_PROP_SET_UNSUPPORTED;
	if (dwPropID != AMPROPERTY_PIN_CATEGORY)        return E_PROP_ID_UNSUPPORTED;
	if (pPropData == NULL && pcbReturned == NULL)   return E_POINTER;

	if (pcbReturned) *pcbReturned = sizeof(GUID);
	if (pPropData == NULL)          return S_OK; 
	if (cbPropData < sizeof(GUID))  return E_UNEXPECTED;

	*(GUID *)pPropData = PIN_CATEGORY_CAPTURE;
	return S_OK;
}

HRESULT CVCamStream::QuerySupported(REFGUID guidPropSet, DWORD dwPropID, 
	DWORD *pTypeSupport)
{
	if (guidPropSet != AMPROPSETID_Pin) return E_PROP_SET_UNSUPPORTED;
	if (dwPropID != AMPROPERTY_PIN_CATEGORY) return E_PROP_ID_UNSUPPORTED;
	if (pTypeSupport) *pTypeSupport = KSPROPERTY_SUPPORT_GET;
	return S_OK;
}

void CVCamStream::LoadPlaceholder() {
	if (initialize_placeholder()) {
		placeholder.source_data = get_placeholder_ptr();
		get_placeholder_size(&placeholder.cx, &placeholder.cy);
	} else {
		placeholder.source_data = nullptr;
	}

	/* Created dynamically based on output resolution changes */
	placeholder.scaled_data = nullptr;

	nv12_scale_init(&scaler, TARGET_FORMAT_YUY2, cx, cy, cx, cy);
	nv12_scale_init(&placeholder.scaler, TARGET_FORMAT_YUY2, cx, cy,
			placeholder.cx, placeholder.cy);

	UpdatePlaceholder();
}

/* Called when the output resolution or format has changed to re-scale
   the placeholder graphic into the placeholder.scaled_data buffer. */
void CVCamStream::UpdatePlaceholder(void)
{
	if (!placeholder.source_data)
		return;

	if (placeholder.scaled_data)
		free(placeholder.scaled_data);

	placeholder.scaled_data = (uint8_t *)malloc(GetOutputBufferSize());
	if (!placeholder.scaled_data)
		return;

	if (placeholder.cx == GetCX() && placeholder.cy == GetCY() &&
	    placeholder.scaler.format == TARGET_FORMAT_NV12) {
		/* No scaling necessary if it matches exactly */
		memcpy(placeholder.scaled_data, placeholder.source_data,
		       GetOutputBufferSize());
	} else {
		nv12_scale_init(&placeholder.scaler, placeholder.scaler.format,
				GetCX(), GetCY(), placeholder.cx,
				placeholder.cy);
		nv12_do_scale(&placeholder.scaler, placeholder.scaled_data,
			      placeholder.source_data);
	}
}

WORD CVCamStream::GetFormatBits(target_format format)
{
	switch (format) {
	/* planar YUV formats */
	case TARGET_FORMAT_I420:
	case TARGET_FORMAT_NV12:
		return 12;
	/* packed YUV formats */
	case TARGET_FORMAT_YUY2:
		return 16;
	default:
		return 0;
	}
}

/* Calculate the size of the output buffer based on the filter's
   resolution and format */
const int CVCamStream::GetOutputBufferSize(void)
{
	int bits = GetFormatBits(TARGET_FORMAT_YUY2);
	return GetCX() * GetCY() * bits / 8;
}


void CVCamStream::ShowDefaultFrame(uint8_t *ptr, IMediaSample *pms)
{
	if (placeholder.scaled_data) {
		// memset(ptr, 0,  GetOutputBufferSize());
		memcpy(ptr, placeholder.scaled_data,  pms->GetActualDataLength());
	} else {
		int size = pms->GetActualDataLength();
		memset(ptr, 127, size);
	}
}

int CVCamStream::GetCX() {

	VIDEOINFOHEADER *pvi = (VIDEOINFOHEADER *)(m_mt.Format());
	return pvi->bmiHeader.biWidth;
	// return 1280;
}

int CVCamStream::GetCY() {
	VIDEOINFOHEADER *pvi = (VIDEOINFOHEADER *)(m_mt.Format());
	return pvi->bmiHeader.biHeight;
	// return 720;
}