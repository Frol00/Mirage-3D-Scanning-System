// Network RealSense source for rs-server/librealsense2-net.

#include "RealSense2NetworkEngine.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

#ifdef COMPILE_WITH_RealSense2Net
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <librealsense2/rs.hpp>
#include <librealsense2-net/rs_net.hpp>

using namespace InputSource;
using namespace ITMLib;

namespace
{
	int ToRealSenseVisualPreset(int presetIndex)
	{
		switch (presetIndex)
		{
		case 1: return RS2_RS400_VISUAL_PRESET_HIGH_DENSITY;
		case 2: return RS2_RS400_VISUAL_PRESET_HIGH_ACCURACY;
		case 0:
		default: return RS2_RS400_VISUAL_PRESET_DEFAULT;
		}
	}

	void ApplyDepthSensorOptions(const rs2::device& device, int presetIndex, int laserPower)
	{
		for (rs2::sensor sensor : device.query_sensors())
		{
			if (rs2::depth_sensor depthSensor = sensor.as<rs2::depth_sensor>())
			{
				if (depthSensor.supports(RS2_OPTION_VISUAL_PRESET))
					depthSensor.set_option(RS2_OPTION_VISUAL_PRESET, static_cast<float>(ToRealSenseVisualPreset(presetIndex)));

				if (depthSensor.supports(RS2_OPTION_LASER_POWER))
					depthSensor.set_option(RS2_OPTION_LASER_POWER, static_cast<float>(laserPower));

				return;
			}
		}
	}

	int PreferredFpsForResolution(const Vector2i& resolution)
	{
		if (resolution.x == 1280 && resolution.y == 720) return 6;
		return 15;
	}

	bool SelectVideoProfile(const rs2::sensor& sensor, rs2_stream streamType, rs2_format format,
							const Vector2i& size, int fps, rs2::stream_profile& selected)
	{
		for (const rs2::stream_profile& profile : sensor.get_stream_profiles())
		{
			if (profile.stream_type() != streamType) continue;
			if (profile.format() != format) continue;
			if (profile.fps() != fps) continue;

			rs2::video_stream_profile videoProfile = profile.as<rs2::video_stream_profile>();
			if (videoProfile.width() != size.x || videoProfile.height() != size.y) continue;

			selected = profile;
			return true;
		}

		return false;
	}

	std::string SensorName(const rs2::sensor& sensor)
	{
		if (sensor.supports(RS2_CAMERA_INFO_NAME)) return sensor.get_info(RS2_CAMERA_INFO_NAME);
		return "unknown sensor";
	}

	struct NetworkEndpoint
	{
		std::string host;
		int port;
	};

	std::string Trim(const std::string& value)
	{
		size_t begin = 0;
		while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;

		size_t end = value.size();
		while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;

		return value.substr(begin, end - begin);
	}

	NetworkEndpoint ParseNetworkEndpoint(const std::string& address)
	{
		std::string trimmed = Trim(address);
		const std::string rtspPrefix = "rtsp://";
		if (trimmed.compare(0, rtspPrefix.size(), rtspPrefix) == 0)
			trimmed = trimmed.substr(rtspPrefix.size());

		const size_t slash = trimmed.find('/');
		if (slash != std::string::npos) trimmed = trimmed.substr(0, slash);

		NetworkEndpoint endpoint;
		endpoint.host = trimmed;
		endpoint.port = 8554;

		const size_t colon = trimmed.rfind(':');
		if (colon != std::string::npos && trimmed.find(':') == colon)
		{
			endpoint.host = trimmed.substr(0, colon);
			try { endpoint.port = std::stoi(trimmed.substr(colon + 1)); }
			catch (...) { endpoint.port = 8554; }
		}

		endpoint.host = Trim(endpoint.host);
		if (endpoint.host.empty())
			throw std::runtime_error("Network RealSense IP address is empty.");

		return endpoint;
	}

#ifdef _WIN32
	bool TryConnectSocket(addrinfo *info, int timeoutMs, std::string& error)
	{
		SOCKET s = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
		if (s == INVALID_SOCKET)
		{
			error = "socket() failed.";
			return false;
		}

		u_long nonBlocking = 1;
		ioctlsocket(s, FIONBIO, &nonBlocking);

		const int connectResult = connect(s, info->ai_addr, static_cast<int>(info->ai_addrlen));
		if (connectResult == 0)
		{
			closesocket(s);
			return true;
		}

		const int connectError = WSAGetLastError();
		if (connectError != WSAEWOULDBLOCK && connectError != WSAEINPROGRESS && connectError != WSAEINVAL)
		{
			std::ostringstream msg;
			msg << "connect() failed with WSA error " << connectError << ".";
			error = msg.str();
			closesocket(s);
			return false;
		}

		fd_set writeSet;
		FD_ZERO(&writeSet);
		FD_SET(s, &writeSet);

		timeval timeout;
		timeout.tv_sec = timeoutMs / 1000;
		timeout.tv_usec = (timeoutMs % 1000) * 1000;

		const int selectResult = select(0, NULL, &writeSet, NULL, &timeout);
		if (selectResult > 0 && FD_ISSET(s, &writeSet))
		{
			int soError = 0;
			int soErrorLen = sizeof(soError);
			getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soError), &soErrorLen);
			closesocket(s);
			if (soError == 0) return true;

			std::ostringstream msg;
			msg << "connect() completed with WSA error " << soError << ".";
			error = msg.str();
			return false;
		}

		closesocket(s);
		error = "connection timeout.";
		return false;
	}
#else
	bool TryConnectSocket(addrinfo *info, int timeoutMs, std::string& error)
	{
		int s = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
		if (s < 0)
		{
			error = "socket() failed.";
			return false;
		}

		const int flags = fcntl(s, F_GETFL, 0);
		fcntl(s, F_SETFL, flags | O_NONBLOCK);

		const int connectResult = connect(s, info->ai_addr, info->ai_addrlen);
		if (connectResult == 0)
		{
			close(s);
			return true;
		}

		if (errno != EINPROGRESS)
		{
			error = "connect() failed.";
			close(s);
			return false;
		}

		fd_set writeSet;
		FD_ZERO(&writeSet);
		FD_SET(s, &writeSet);

		timeval timeout;
		timeout.tv_sec = timeoutMs / 1000;
		timeout.tv_usec = (timeoutMs % 1000) * 1000;

		const int selectResult = select(s + 1, NULL, &writeSet, NULL, &timeout);
		if (selectResult > 0 && FD_ISSET(s, &writeSet))
		{
			int soError = 0;
			socklen_t soErrorLen = sizeof(soError);
			getsockopt(s, SOL_SOCKET, SO_ERROR, &soError, &soErrorLen);
			close(s);
			if (soError == 0) return true;

			error = "connect() completed with socket error.";
			return false;
		}

		close(s);
		error = "connection timeout.";
		return false;
	}
#endif

	bool CanConnectTcp(const NetworkEndpoint& endpoint, int timeoutMs, std::string& error)
	{
#ifdef _WIN32
		WSADATA wsaData;
		const int wsaResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
		if (wsaResult != 0)
		{
			error = "WSAStartup failed.";
			return false;
		}
#endif

		addrinfo hints;
		std::memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;

		addrinfo *result = NULL;
		const std::string portString = std::to_string(endpoint.port);
		const int gaiResult = getaddrinfo(endpoint.host.c_str(), portString.c_str(), &hints, &result);
		if (gaiResult != 0)
		{
#ifdef _WIN32
			WSACleanup();
#endif
			error = "could not resolve host.";
			return false;
		}

		bool connected = false;
		for (addrinfo *it = result; it != NULL && !connected; it = it->ai_next)
			connected = TryConnectSocket(it, timeoutMs, error);

		freeaddrinfo(result);
#ifdef _WIN32
		WSACleanup();
#endif
		return connected;
	}

	unsigned char ClampToByte(int value)
	{
		return static_cast<unsigned char>(std::max(0, std::min(255, value)));
	}

	Vector4u YuvToRgba(int y, int u, int v)
	{
		const int c = y - 16;
		const int d = u - 128;
		const int e = v - 128;
		const int r = (298 * c + 409 * e + 128) >> 8;
		const int g = (298 * c - 100 * d - 208 * e + 128) >> 8;
		const int b = (298 * c + 516 * d + 128) >> 8;
		return Vector4u(ClampToByte(r), ClampToByte(g), ClampToByte(b), 255);
	}

	void ConvertColorFrameToRgba(const rs2::video_frame& color, ITMUChar4Image *rgbImage)
	{
		Vector4u *rgb = rgbImage->GetData(MEMORYDEVICE_CPU);
		const int width = rgbImage->noDims.x;
		const int height = rgbImage->noDims.y;
		const int pixelCount = width * height;
		const rs2_format format = color.get_profile().as<rs2::video_stream_profile>().format();
		const unsigned char *src = reinterpret_cast<const unsigned char*>(color.get_data());

		switch (format)
		{
		case RS2_FORMAT_RGBA8:
			std::memcpy(rgb, src, sizeof(Vector4u) * pixelCount);
			break;

		case RS2_FORMAT_BGRA8:
			for (int i = 0; i < pixelCount; ++i)
				rgb[i] = Vector4u(src[i * 4 + 2], src[i * 4 + 1], src[i * 4 + 0], src[i * 4 + 3]);
			break;

		case RS2_FORMAT_RGB8:
			for (int i = 0; i < pixelCount; ++i)
				rgb[i] = Vector4u(src[i * 3 + 0], src[i * 3 + 1], src[i * 3 + 2], 255);
			break;

		case RS2_FORMAT_BGR8:
			for (int i = 0; i < pixelCount; ++i)
				rgb[i] = Vector4u(src[i * 3 + 2], src[i * 3 + 1], src[i * 3 + 0], 255);
			break;

		case RS2_FORMAT_YUYV:
			for (int i = 0; i < pixelCount; i += 2)
			{
				const int base = i * 2;
				const int y0 = src[base + 0];
				const int u = src[base + 1];
				const int y1 = src[base + 2];
				const int v = src[base + 3];
				rgb[i] = YuvToRgba(y0, u, v);
				if (i + 1 < pixelCount) rgb[i + 1] = YuvToRgba(y1, u, v);
			}
			break;

		case RS2_FORMAT_UYVY:
			for (int i = 0; i < pixelCount; i += 2)
			{
				const int base = i * 2;
				const int u = src[base + 0];
				const int y0 = src[base + 1];
				const int v = src[base + 2];
				const int y1 = src[base + 3];
				rgb[i] = YuvToRgba(y0, u, v);
				if (i + 1 < pixelCount) rgb[i + 1] = YuvToRgba(y1, u, v);
			}
			break;

		default:
			for (int i = 0; i < pixelCount; ++i) rgb[i] = Vector4u(0, 0, 0, 255);
			break;
		}
	}
}

RealSense2NetworkEngine::RealSense2NetworkEngine(const char *calibFilename, const std::string& serverAddress,
												 bool alignColourWithDepth,
												 Vector2i requested_imageSize_rgb, Vector2i requested_imageSize_d,
												 int presetIndex, int laserPower)
: BaseImageSourceEngine(calibFilename), serverAddress(serverAddress)
{
	(void)alignColourWithDepth;
	(void)presetIndex;
	(void)laserPower;

	this->calib.disparityCalib.SetStandard();
	this->calib.trafo_rgb_to_depth = ITMExtrinsics();
	this->calib.intrinsics_d = this->calib.intrinsics_rgb;

	this->imageSize_d = requested_imageSize_d;
	this->imageSize_rgb = requested_imageSize_rgb;
	this->dataAvailable = false;
	this->pipelineError = false;
	this->depthSensorOpened = false;
	this->colorSensorOpened = false;
	this->depthSensorStarted = false;
	this->colorSensorStarted = false;

	if (serverAddress.empty())
		throw std::runtime_error("Network RealSense IP address is empty.");
	if (imageSize_d.x == 848 && imageSize_d.y == 480)
		throw std::runtime_error("Network mode with stock rs-server does not expose matching RGB at 848x480. Use 640x480 or 1280x720.");

	const NetworkEndpoint endpoint = ParseNetworkEndpoint(serverAddress);
	std::string tcpError;
	std::cout << "Checking RealSense rs-server RTSP port "
			  << endpoint.host << ":" << endpoint.port << std::endl;
	if (!CanConnectTcp(endpoint, 2500, tcpError))
	{
		throw std::runtime_error("Cannot connect to rs-server RTSP port "
			+ endpoint.host + ":" + std::to_string(endpoint.port)
			+ " (" + tcpError + "). Restart mirage-rs-server on Raspberry Pi and check firewall/IP.");
	}
	std::cout << "RealSense rs-server RTSP port is reachable." << std::endl;

	this->ctx = std::unique_ptr<rs2::context>(new rs2::context());

	std::cout << "Connecting to RealSense rs-server at " << serverAddress << std::endl;
	this->netDevice = std::unique_ptr<rs2::net_device>(new rs2::net_device(serverAddress));
	this->netDevice->add_to(*ctx);
	this->device = std::unique_ptr<rs2::device>(new rs2::device(*netDevice));

	if (device->supports(RS2_CAMERA_INFO_NAME))
		std::cout << "Network RealSense device: " << device->get_info(RS2_CAMERA_INFO_NAME) << std::endl;
	if (device->supports(RS2_CAMERA_INFO_SERIAL_NUMBER))
		std::cout << "Network RealSense serial: " << device->get_info(RS2_CAMERA_INFO_SERIAL_NUMBER) << std::endl;

	for (rs2::sensor sensor : device->query_sensors())
	{
		if (rs2::depth_sensor depthSensor = sensor.as<rs2::depth_sensor>())
		{
			const float scale = depthSensor.get_depth_scale();
			std::cout << "Network depth scale factor is: " << scale << std::endl;
			this->calib.disparityCalib.SetFrom(scale, 0, ITMLib::ITMDisparityCalib::TRAFO_AFFINE);
		}
	}

	std::cout << "Network RealSense: skipping remote D415 preset/laser options to avoid blocking rs-server control commands." << std::endl;

	const int fps = PreferredFpsForResolution(imageSize_d);
	rs2::sensor selectedDepthSensor;
	rs2::sensor selectedColorSensor;
	rs2::stream_profile selectedDepthProfile;
	rs2::stream_profile selectedColorProfile;

	for (const rs2::sensor& sensor : device->query_sensors())
	{
		if (!selectedDepthProfile)
		{
			rs2::stream_profile profile;
			if (SelectVideoProfile(sensor, RS2_STREAM_DEPTH, RS2_FORMAT_Z16, imageSize_d, fps, profile))
			{
				selectedDepthSensor = sensor;
				selectedDepthProfile = profile;
				std::cout << "Network RealSense: selected depth profile from " << SensorName(sensor) << std::endl;
			}
		}

		if (!selectedColorProfile)
		{
			rs2::stream_profile profile;
			if (SelectVideoProfile(sensor, RS2_STREAM_COLOR, RS2_FORMAT_RGB8, imageSize_rgb, fps, profile))
			{
				selectedColorSensor = sensor;
				selectedColorProfile = profile;
				std::cout << "Network RealSense: selected color profile from " << SensorName(sensor) << std::endl;
			}
		}
	}

	if (!selectedDepthProfile)
		throw std::runtime_error("Network RealSense depth profile was not found.");
	if (!selectedColorProfile)
		throw std::runtime_error("Network RealSense color RGB8 profile was not found.");

	this->depthSensor = std::unique_ptr<rs2::sensor>(new rs2::sensor(selectedDepthSensor));
	this->colorSensor = std::unique_ptr<rs2::sensor>(new rs2::sensor(selectedColorSensor));
	this->depthProfile = std::unique_ptr<rs2::stream_profile>(new rs2::stream_profile(selectedDepthProfile));
	this->colorProfile = std::unique_ptr<rs2::stream_profile>(new rs2::stream_profile(selectedColorProfile));
	this->depthQueue = std::unique_ptr<rs2::frame_queue>(new rs2::frame_queue(4));
	this->colorQueue = std::unique_ptr<rs2::frame_queue>(new rs2::frame_queue(4));

	rs2::video_stream_profile depth_stream_profile = depthProfile->as<rs2::video_stream_profile>();
	rs2::video_stream_profile color_stream_profile = colorProfile->as<rs2::video_stream_profile>();

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

	std::cout << "Network RealSense: opening direct sensors "
			  << imageSize_d.x << "x" << imageSize_d.y
			  << " @" << fps << " depth Z16, color RGB8" << std::endl;

	try
	{
		depthSensor->open(*depthProfile);
		depthSensorOpened = true;
		colorSensor->open(*colorProfile);
		colorSensorOpened = true;

		depthSensor->start(*depthQueue);
		depthSensorStarted = true;
		colorSensor->start(*colorQueue);
		colorSensorStarted = true;

		std::cout << "Network RealSense direct sensors started. Waiting for first frames..." << std::endl;

		rs2::frame depthFrame;
		rs2::frame colorFrame;
		if (!depthQueue->try_wait_for_frame(&depthFrame, 5000))
			throw std::runtime_error("Timed out waiting for first network depth frame.");
		if (!colorQueue->try_wait_for_frame(&colorFrame, 5000))
			throw std::runtime_error("Timed out waiting for first network color frame.");
	}
	catch (...)
	{
		try { if (colorSensor && colorSensorStarted) colorSensor->stop(); } catch (...) {}
		try { if (depthSensor && depthSensorStarted) depthSensor->stop(); } catch (...) {}
		try { if (colorSensor && colorSensorOpened) colorSensor->close(); } catch (...) {}
		try { if (depthSensor && depthSensorOpened) depthSensor->close(); } catch (...) {}
		colorSensorStarted = false;
		depthSensorStarted = false;
		colorSensorOpened = false;
		depthSensorOpened = false;
		throw;
	}

	std::cout << "Network RealSense stream started at "
			  << imageSize_d.x << "x" << imageSize_d.y
			  << " @" << fps << " fps" << std::endl;
}

RealSense2NetworkEngine::~RealSense2NetworkEngine()
{
	try
	{
		if (colorSensor && colorSensorStarted) colorSensor->stop();
	}
	catch (...) {}

	try
	{
		if (depthSensor && depthSensorStarted) depthSensor->stop();
	}
	catch (...) {}

	try
	{
		if (colorSensor && colorSensorOpened) colorSensor->close();
	}
	catch (...) {}

	try
	{
		if (depthSensor && depthSensorOpened) depthSensor->close();
	}
	catch (...) {}
}

void RealSense2NetworkEngine::getImages(ITMUChar4Image *rgbImage, ITMShortImage *rawDepthImage)
{
	dataAvailable = false;
	pipelineError = false;

	try {
		rs2::frame depthFrame;
		rs2::frame colorFrame;
		// Use a short timeout (200ms) so the caller's no-frame counter accumulates
		// at the camera's true frame rate rather than stalling for 3 full seconds.
		if (!depthQueue->try_wait_for_frame(&depthFrame, 200) ||
		    !colorQueue->try_wait_for_frame(&colorFrame, 200))
			return; // no frame this cycle — caller counts consecutive misses

		rs2::video_frame depth = depthFrame.as<rs2::video_frame>();
		rs2::video_frame color = colorFrame.as<rs2::video_frame>();
		if (!depth || !color) return;

		const uint16_t *depth_data = reinterpret_cast<const uint16_t *>(depth.get_data());
		short *rawDepth = rawDepthImage->GetData(MEMORYDEVICE_CPU);
		std::memcpy(rawDepth, depth_data, sizeof(uint16_t) * rawDepthImage->noDims.x * rawDepthImage->noDims.y);

		ConvertColorFrameToRgba(color, rgbImage);
		dataAvailable = true;
	} catch (const std::exception&) {
		pipelineError = true;
	} catch (...) {
		pipelineError = true;
	}
}

bool RealSense2NetworkEngine::hasMoreImages(void) const
{
	return depthSensorStarted && colorSensorStarted;
}

Vector2i RealSense2NetworkEngine::getDepthImageSize(void) const
{
	return (depthSensorStarted && colorSensorStarted) ? imageSize_d : Vector2i(0, 0);
}

Vector2i RealSense2NetworkEngine::getRGBImageSize(void) const
{
	return (depthSensorStarted && colorSensorStarted) ? imageSize_rgb : Vector2i(0, 0);
}

#else

using namespace InputSource;

RealSense2NetworkEngine::RealSense2NetworkEngine(const char *calibFilename, const std::string& serverAddress,
												 bool alignColourWithDepth,
												 Vector2i requested_imageSize_rgb, Vector2i requested_imageSize_d,
												 int presetIndex, int laserPower)
: BaseImageSourceEngine(calibFilename), dataAvailable(false), serverAddress(serverAddress),
  imageSize_rgb(requested_imageSize_rgb), imageSize_d(requested_imageSize_d)
{
	(void)alignColourWithDepth;
	(void)presetIndex;
	(void)laserPower;
	printf("compiled without RealSense SDK 2.X network support\n");
}

RealSense2NetworkEngine::~RealSense2NetworkEngine()
{}

void RealSense2NetworkEngine::getImages(ITMUChar4Image *rgbImage, ITMShortImage *rawDepthImage)
{
	(void)rgbImage;
	(void)rawDepthImage;
}

bool RealSense2NetworkEngine::hasMoreImages(void) const
{
	return false;
}

Vector2i RealSense2NetworkEngine::getDepthImageSize(void) const
{
	return Vector2i(0, 0);
}

Vector2i RealSense2NetworkEngine::getRGBImageSize(void) const
{
	return Vector2i(0, 0);
}

#endif
