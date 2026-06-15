// Copyright Oxford University Innovation Limited and the authors of InfiniTAM

#pragma once

#include "ImageSourceEngine.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#ifdef COMPILE_WITH_RealSense2
#include <librealsense2/rs.hpp>
#endif

namespace InputSource {

	class RealSense2Engine : public BaseImageSourceEngine
	{
	private:
		bool dataAvailable;
		bool pipelineError;

#ifdef COMPILE_WITH_RealSense2
		std::unique_ptr<rs2::context> ctx;
		std::unique_ptr<rs2::device> device;
		std::unique_ptr<rs2::pipeline> pipe;

		// Background reader thread — polls RS2 continuously so the D415 USB
		// watchdog never fires regardless of how long main-thread processing takes.
		// getImages() waits on latestCv_ so stride-skip cycles don't accumulate
		// false "no frame" counts just because the previous frame was consumed
		// one idle tick ago.
		std::thread readerThread_;
		std::atomic<bool> readerRunning_{false};
		std::mutex latestMutex_;
		std::condition_variable latestCv_;
		std::unique_ptr<rs2::frameset> latestFrameset_;
		bool latestAvailable_{false};
		std::atomic<bool> readerPipelineError_{false};

		void ReaderThreadMain();
#endif

		Vector2i imageSize_rgb, imageSize_d;

	public:
		// Counts background pipe->stop() threads that are still running.
		// TickCameraReinit waits for this to reach 0 before opening a new pipeline
		// to prevent concurrent RS2 operations on the same USB device from crashing.
		static std::atomic<int> activePipeStopCount;
		RealSense2Engine(const char *calibFilename, bool alignColourWithDepth = true,
						 Vector2i imageSize_rgb = Vector2i(640, 480), Vector2i imageSize_d = Vector2i(640, 480),
						 int visualPreset = -1, int laserPower = -1);
		~RealSense2Engine();

		bool hasMoreImages(void) const;
		bool hasImagesNow(void) const override { return dataAvailable; }
		bool hasPipelineError(void) const override { return pipelineError; }
		void getImages(ITMUChar4Image *rgb, ITMShortImage *rawDepth);
		Vector2i getDepthImageSize(void) const;
		Vector2i getRGBImageSize(void) const;
	};

}
