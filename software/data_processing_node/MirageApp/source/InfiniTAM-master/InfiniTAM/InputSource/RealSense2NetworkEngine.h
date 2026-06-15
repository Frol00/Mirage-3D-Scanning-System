// Network RealSense source for rs-server/librealsense2-net.

#pragma once

#include "ImageSourceEngine.h"

#include <memory>
#include <string>

#ifdef COMPILE_WITH_RealSense2Net
namespace rs2 { class context; class device; class frame_queue; class net_device; class sensor; class stream_profile; }
#endif

namespace InputSource {

	class RealSense2NetworkEngine : public BaseImageSourceEngine
	{
	private:
		bool dataAvailable;
		bool pipelineError;
		std::string serverAddress;

#ifdef COMPILE_WITH_RealSense2Net
		std::unique_ptr<rs2::context> ctx;
		std::unique_ptr<rs2::net_device> netDevice;
		std::unique_ptr<rs2::device> device;
		std::unique_ptr<rs2::sensor> depthSensor;
		std::unique_ptr<rs2::sensor> colorSensor;
		std::unique_ptr<rs2::stream_profile> depthProfile;
		std::unique_ptr<rs2::stream_profile> colorProfile;
		std::unique_ptr<rs2::frame_queue> depthQueue;
		std::unique_ptr<rs2::frame_queue> colorQueue;
		bool depthSensorOpened;
		bool colorSensorOpened;
		bool depthSensorStarted;
		bool colorSensorStarted;
#endif

		Vector2i imageSize_rgb, imageSize_d;

	public:
		RealSense2NetworkEngine(const char *calibFilename, const std::string& serverAddress,
								bool alignColourWithDepth = true,
								Vector2i imageSize_rgb = Vector2i(640, 480), Vector2i imageSize_d = Vector2i(640, 480),
								int presetIndex = 0, int laserPower = 150);
		~RealSense2NetworkEngine();

		bool hasMoreImages(void) const;
		bool hasImagesNow(void) const override { return dataAvailable; }
		bool hasPipelineError(void) const override { return pipelineError; }
		void getImages(ITMUChar4Image *rgb, ITMShortImage *rawDepth);
		Vector2i getDepthImageSize(void) const;
		Vector2i getRGBImageSize(void) const;
	};

}
