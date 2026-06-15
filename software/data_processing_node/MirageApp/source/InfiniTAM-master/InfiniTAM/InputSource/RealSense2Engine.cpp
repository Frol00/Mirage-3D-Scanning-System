// Copyright 2014-2017 Oxford University Innovation Limited and the authors of InfiniTAM

#include "RealSense2Engine.h"

#include "../ORUtils/FileUtils.h"

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <thread>

#include <iostream>
#include <iomanip>

#ifdef COMPILE_WITH_RealSense2
#include "librealsense2/rs.hpp"

using namespace InputSource;
using namespace ITMLib;

std::atomic<int> RealSense2Engine::activePipeStopCount{0};

static void print_device_information(const rs2::device& dev)
{
	// Each device provides some information on itself
	// The different types of available information are represented using the "RS2_CAMERA_INFO_*" enum
	
	std::cout << "Device information: " << std::endl;
	//The following code shows how to enumerate all of the RS2_CAMERA_INFO
	//Note that all enum types in the SDK start with the value of zero and end at the "*_COUNT" value
	for (int i = 0; i < static_cast<int>(RS2_CAMERA_INFO_COUNT); i++)
	{
		rs2_camera_info info_type = static_cast<rs2_camera_info>(i);
		//SDK enum types can be streamed to get a string that represents them
		std::cout << "  " << std::left << std::setw(20) << info_type << " : ";
		
		//A device might not support all types of RS2_CAMERA_INFO.
		//To prevent throwing exceptions from the "get_info" method we first check if the device supports this type of info
		if (dev.supports(info_type))
			std::cout << dev.get_info(info_type) << std::endl;
		else
			std::cout << "N/A" << std::endl;
	}
}


RealSense2Engine::RealSense2Engine(const char *calibFilename, bool alignColourWithDepth,
								   Vector2i requested_imageSize_rgb, Vector2i requested_imageSize_d,
								   int visualPreset, int laserPower)
: BaseImageSourceEngine(calibFilename)
{
	this->dataAvailable = false;
	this->pipelineError = false;

	this->calib.disparityCalib.SetStandard();
	this->calib.trafo_rgb_to_depth = ITMExtrinsics();
	this->calib.intrinsics_d = this->calib.intrinsics_rgb;

	this->imageSize_d = requested_imageSize_d;
	this->imageSize_rgb = requested_imageSize_rgb;
	
	this->ctx = std::unique_ptr<rs2::context>(new rs2::context());
	
	rs2::device_list availableDevices = ctx->query_devices();
	
	printf("There are %d connected RealSense devices.\n", availableDevices.size());
	if (availableDevices.size() == 0) {
		dataAvailable = false;
		ctx.reset();
		return;
	}
	
	this->device = std::unique_ptr<rs2::device>(new rs2::device(availableDevices.front()));
	
	print_device_information(*device);
	
	this->pipe = std::unique_ptr<rs2::pipeline>(new rs2::pipeline(*ctx));

	// Query sensors first so we can determine the best available fps
	// before building the pipeline config (was previously done after).
	auto availableSensors = device->query_sensors();
	std::cout << "Device consists of " << availableSensors.size() << " sensors:" << std::endl;

	int depthBestFps = 0, colorBestFps = 0;
	for (rs2::sensor& sensor : availableSensors)
	{
		bool isDepthSensor = static_cast<bool>(sensor.as<rs2::depth_sensor>());

		if (isDepthSensor)
		{
			rs2::depth_sensor dpt = sensor.as<rs2::depth_sensor>();
			float scale = dpt.get_depth_scale();
			std::cout << "Depth sensor scale: " << scale << std::endl;
			this->calib.disparityCalib.SetFrom(scale, 0, ITMLib::ITMDisparityCalib::TRAFO_AFFINE);
		}

		for (rs2::stream_profile& sp : sensor.get_stream_profiles())
		{
			rs2::video_stream_profile vp = sp.as<rs2::video_stream_profile>();
			if (!vp) continue;

			if (isDepthSensor
				&& vp.stream_type() == RS2_STREAM_DEPTH
				&& vp.width() == imageSize_d.x
				&& vp.height() == imageSize_d.y)
			{
				depthBestFps = std::max(depthBestFps, vp.fps());
			}
			else if (!isDepthSensor
				&& vp.width() == imageSize_rgb.x
				&& vp.height() == imageSize_rgb.y)
			{
				// Color sensor: check any format — RGBA8 is converted from these natively.
				colorBestFps = std::max(colorBestFps, vp.fps());
			}
		}
	}

	// Cap at 30fps: higher rates (60/90) over combined depth+color can saturate USB bandwidth.
	// Use the minimum of depth and color best fps so both streams can run in sync.
	const int streamFps = std::max(6, std::min({depthBestFps, colorBestFps, 30}));
	std::cout << "Stream fps selected: " << streamFps
		<< "  (depth max: " << depthBestFps
		<< ", color max: " << colorBestFps << ")" << std::endl;

	rs2::config config;
	config.enable_stream(RS2_STREAM_DEPTH, imageSize_d.x, imageSize_d.y, RS2_FORMAT_Z16, streamFps);
	config.enable_stream(RS2_STREAM_COLOR, imageSize_rgb.x, imageSize_rgb.y, RS2_FORMAT_RGBA8, streamFps);
	
	rs2::pipeline_profile pipeline_profile = pipe->start(config);

	// Apply sensor options after pipeline start to avoid device firmware race condition.
	// Setting options before start() with a separate context causes 0x8007001f on High Accuracy.
	if (visualPreset >= 0 || laserPower >= 0)
	{
		for (rs2::sensor sensor : availableSensors)
		{
			if (rs2::depth_sensor depthSensor = sensor.as<rs2::depth_sensor>())
			{
				if (visualPreset >= 0 && depthSensor.supports(RS2_OPTION_VISUAL_PRESET))
					depthSensor.set_option(RS2_OPTION_VISUAL_PRESET, static_cast<float>(visualPreset));
				if (laserPower >= 0 && depthSensor.supports(RS2_OPTION_LASER_POWER))
					depthSensor.set_option(RS2_OPTION_LASER_POWER, static_cast<float>(laserPower));
				break;
			}
		}
	}

	// Start background reader thread — keeps RS2 frames drained at all times so
	// the D415 USB watchdog never sees the host "going silent" regardless of how
	// long main-thread TSDF processing or OpenGL rendering takes.
	readerRunning_.store(true, std::memory_order_relaxed);
	readerThread_ = std::thread(&RealSense2Engine::ReaderThreadMain, this);

	rs2::video_stream_profile depth_stream_profile = pipeline_profile.get_stream(RS2_STREAM_DEPTH).as<rs2::video_stream_profile>();
	rs2::video_stream_profile color_stream_profile = pipeline_profile.get_stream(RS2_STREAM_COLOR).as<rs2::video_stream_profile>();
	
	// - intrinsics
	rs2_intrinsics intrinsics_depth = depth_stream_profile.get_intrinsics();
	rs2_intrinsics intrinsics_rgb = color_stream_profile.get_intrinsics();
	
	this->calib.intrinsics_d.projectionParamsSimple.fx = intrinsics_depth.fx;
	this->calib.intrinsics_d.projectionParamsSimple.fy = intrinsics_depth.fy;
	this->calib.intrinsics_d.projectionParamsSimple.px = intrinsics_depth.ppx;
	this->calib.intrinsics_d.projectionParamsSimple.py = intrinsics_depth.ppy;
	
	this->calib.intrinsics_rgb.projectionParamsSimple.fx = intrinsics_rgb.fx;
	this->calib.intrinsics_rgb.projectionParamsSimple.fy = intrinsics_rgb.fy;
	this->calib.intrinsics_rgb.projectionParamsSimple.px = intrinsics_rgb.ppx;
	this->calib.intrinsics_rgb.projectionParamsSimple.py = intrinsics_rgb.ppy;
	
	// - extrinsics
	rs2_extrinsics rs_extrinsics = color_stream_profile.get_extrinsics_to(depth_stream_profile);
	
	Matrix4f extrinsics;
	extrinsics.m00 = rs_extrinsics.rotation[0]; extrinsics.m10 = rs_extrinsics.rotation[1]; extrinsics.m20 = rs_extrinsics.rotation[2];
	extrinsics.m01 = rs_extrinsics.rotation[3]; extrinsics.m11 = rs_extrinsics.rotation[4]; extrinsics.m21 = rs_extrinsics.rotation[5];
	extrinsics.m02 = rs_extrinsics.rotation[6]; extrinsics.m12 = rs_extrinsics.rotation[7]; extrinsics.m22 = rs_extrinsics.rotation[8];
	extrinsics.m30 = rs_extrinsics.translation[0];
	extrinsics.m31 = rs_extrinsics.translation[1];
	extrinsics.m32 = rs_extrinsics.translation[2];
	
	extrinsics.m33 = 1.0f;
	extrinsics.m03 = 0.0f; extrinsics.m13 = 0.0f; extrinsics.m23 = 0.0f;
	
	this->calib.trafo_rgb_to_depth.SetFrom(extrinsics);
}

RealSense2Engine::~RealSense2Engine()
{
	// Stop the background reader thread before touching the pipeline.
	// The reader holds pipe alive via poll_for_frames; stopping it first prevents
	// a race between the reader and the detached pipe->stop() thread below.
	readerRunning_.store(false, std::memory_order_relaxed);
	if (readerThread_.joinable())
		readerThread_.join();

	if (pipe)
	{
		// pipe->stop() can hang when the device is in a bad state (USB disconnect/firmware reset).
		// Run it in a detached thread so the main thread stays live.
		// activePipeStopCount lets TickCameraReinit wait until this finishes before
		// opening a new pipeline — concurrent RS2 operations on the same device can crash.
		activePipeStopCount.fetch_add(1, std::memory_order_relaxed);
		auto* rawPipe   = pipe.release();
		auto* rawDevice = device.release();
		auto* rawCtx    = ctx.release();
		std::thread([rawPipe, rawDevice, rawCtx]()
		{
			try { rawPipe->stop(); } catch (...) {}
			delete rawPipe;
			delete rawDevice;
			delete rawCtx;
			RealSense2Engine::activePipeStopCount.fetch_sub(1, std::memory_order_relaxed);
		}).detach();
	}
}

void RealSense2Engine::ReaderThreadMain()
{
	int consecutiveErrors = 0;
	while (readerRunning_.load(std::memory_order_relaxed))
	{
		rs2::frameset tmp;
		try {
			if (pipe->poll_for_frames(&tmp))
			{
				consecutiveErrors = 0;
				{
					std::lock_guard<std::mutex> lock(latestMutex_);
					if (!latestFrameset_)
						latestFrameset_ = std::make_unique<rs2::frameset>();
					*latestFrameset_ = std::move(tmp);
					latestAvailable_ = true;
				}
				latestCv_.notify_one();
			}
			else
			{
				// No frame ready yet — yield for 2ms to avoid spinning the CPU.
				std::this_thread::sleep_for(std::chrono::milliseconds(2));
			}
		} catch (const rs2::error&) {
			// Transient rs2::error (camera warmup glitch, brief USB hiccup) should
			// not kill the thread. Retry up to 30 times with 100ms between attempts
			// (~3 seconds total) before concluding the camera is truly gone.
			// UIEngine's time-based reconnect guard provides additional protection.
			if (++consecutiveErrors >= 30)
			{
				readerPipelineError_.store(true, std::memory_order_relaxed);
				latestCv_.notify_all();
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		} catch (...) {
			readerPipelineError_.store(true, std::memory_order_relaxed);
			latestCv_.notify_all();
			break;
		}
	}
}

void RealSense2Engine::getImages(ITMUChar4Image *rgbImage, ITMShortImage *rawDepthImage)
{
	dataAvailable = false;
	pipelineError = false;

	// Wait for the background reader thread to deliver a fresh frame (up to 100ms).
	// IMPORTANT: the wait predicate is latestAvailable_ only (not pipelineError).
	// This ensures that even when the reader thread has died, getImages() always
	// blocks for the full 100ms before returning, which throttles the idle loop and
	// prevents the error counter from spin-accumulating in milliseconds.
	rs2::frameset frames;
	{
		std::unique_lock<std::mutex> lock(latestMutex_);
		if (!latestAvailable_)
		{
			latestCv_.wait_for(lock, std::chrono::milliseconds(100),
				[this]{ return latestAvailable_; });
		}
		if (!latestAvailable_ || !latestFrameset_)
		{
			// Timed out — propagate pipeline error only if reader thread confirmed one.
			pipelineError = readerPipelineError_.load(std::memory_order_relaxed);
			return;
		}
		// Copy the frameset reference (ref-counted, cheap) and release lock before memcpy.
		frames = *latestFrameset_;
		latestAvailable_ = false;
	}

	rs2::depth_frame depth = frames.get_depth_frame();
	rs2::video_frame color = frames.get_color_frame();

	constexpr size_t rgb_pixel_size = sizeof(Vector4u);
	static_assert(4 == rgb_pixel_size, "sizeof(rgb pixel) must equal 4");
	const Vector4u *color_frame = reinterpret_cast<const Vector4u*>(color.get_data());

	constexpr size_t depth_pixel_size = sizeof(uint16_t);
	static_assert(2 == depth_pixel_size, "sizeof(depth pixel) must equal 2");
	auto depth_frame = reinterpret_cast<const uint16_t *>(depth.get_data());

	short *rawDepth = rawDepthImage->GetData(MEMORYDEVICE_CPU);
	Vector4u *rgb = rgbImage->GetData(MEMORYDEVICE_CPU);

	std::memcpy(rgb, color_frame, rgb_pixel_size * rgbImage->noDims.x * rgbImage->noDims.y);
	std::memcpy(rawDepth, depth_frame, depth_pixel_size * rawDepthImage->noDims.x * rawDepthImage->noDims.y);

	dataAvailable = true;
}

bool RealSense2Engine::hasMoreImages(void) const {
	return pipe != nullptr;
}

Vector2i RealSense2Engine::getDepthImageSize(void) const {
	return pipe ? imageSize_d : Vector2i(0,0);
}

Vector2i RealSense2Engine::getRGBImageSize(void) const {
	return pipe ? imageSize_rgb : Vector2i(0,0);
}

#else

using namespace InputSource;

RealSense2Engine::RealSense2Engine(const char *calibFilename, bool alignColourWithDepth,
								   Vector2i requested_imageSize_rgb, Vector2i requested_imageSize_d,
								   int visualPreset, int laserPower)
: BaseImageSourceEngine(calibFilename)
{
	printf("compiled without RealSense SDK 2.X support\n");
}
RealSense2Engine::~RealSense2Engine()
{}
void RealSense2Engine::getImages(ITMUChar4Image *rgbImage, ITMShortImage *rawDepthImage)
{ return; }
bool RealSense2Engine::hasMoreImages(void) const
{ return false; }
Vector2i RealSense2Engine::getDepthImageSize(void) const
{ return Vector2i(0,0); }
Vector2i RealSense2Engine::getRGBImageSize(void) const
{ return Vector2i(0,0); }

#endif

