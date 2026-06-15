// Copyright 2014-2017 Oxford University Innovation Limited and the authors of InfiniTAM

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef _MSC_VER
#pragma comment(lib, "Ws2_32.lib")
#endif
#endif

#include "UIEngine.h"
#include "ImGuiGlutBridge.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <iostream>
#include <string.h>
#include <ctime>
#include <sstream>
#include <stdexcept>
#include <thread>

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#endif

#include <imgui.h>
#include <imgui_impl_opengl3.h>

#ifdef FREEGLUT
#include <GL/freeglut.h>
#else
#if (!defined USING_CMAKE) && (defined _MSC_VER)
#pragma comment(lib, "glut64")
#endif
#endif

#include "../../ITMLib/ITMLibDefines.h"
#include "../../ITMLib/Core/ITMBasicEngine.h"
#include "../../ITMLib/Core/ITMBasicSurfelEngine.h"
#include "../../ITMLib/Core/ITMMultiEngine.h"

#include "../../ORUtils/FileUtils.h"
#include "../../InputSource/FFMPEGWriter.h"
#include "../../InputSource/RealSense2Engine.h"
#include "../../InputSource/RealSense2NetworkEngine.h"

#ifdef COMPILE_WITH_RealSense2
#include <librealsense2/rs.hpp>
#endif

using namespace InfiniTAM::Engine;
using namespace InputSource;
using namespace ITMLib;

UIEngine* UIEngine::instance;

static void safe_glutBitmapString(void *font, const char *str)
{
	size_t len = strlen(str);
	for (size_t x = 0; x < len; ++x) {
		glutBitmapCharacter(font, str[x]);
	}
}

namespace
{
	const char *kResolutionNames[] = { "1280 x 720", "848 x 480", "640 x 480" };
	const Vector2i kResolutions[] = { Vector2i(1280, 720), Vector2i(848, 480), Vector2i(640, 480) };
	const char *kPresetNames[] = { "Default", "High Density", "High Accuracy" };
	const char *kFrameStrideNames[] = { "Every frame", "Every 2nd frame", "Every 3rd frame", "Every 4th frame" };
	const int kFrameStrides[] = { 1, 2, 3, 4 };
	const char *kScanAreaNames[] = { "10 x 10 cm", "25 x 25 cm", "50 x 50 cm", "100 x 100 cm" };
	const float kScanAreaSizesM[] = { 0.10f, 0.25f, 0.50f, 1.00f };
	const char *kMeshDetailNames[] = { "Balanced (5 mm)", "Fine (3 mm)", "Ultra (2 mm)" };
	const float kMeshVoxelSizesM[] = { 0.005f, 0.003f, 0.002f };
	const char *kDepthOnlyTrackerConfig =
		"type=extended,levels=rrbb,useDepth=1,minstep=1e-4,"
		"outlierSpaceC=0.1,outlierSpaceF=0.004,"
		"numiterC=20,numiterF=50,tukeyCutOff=8,"
		"framesToSkip=20,framesToWeight=50,failureDec=20.0";
	const char *kColourDepthTrackerConfig =
		"type=extended,levels=bbb,useDepth=1,useColour=1,"
		"colourWeight=0.2,minstep=1e-4,"
		"outlierColourC=0.175,outlierColourF=0.005,"
		"outlierSpaceC=0.1,outlierSpaceF=0.004,"
		"numiterC=8,numiterF=12,tukeyCutOff=8,"
		"framesToSkip=20,framesToWeight=50,failureDec=20.0";
	const int kRemoteControlDefaultPort = 5055;
	const int kRaspberryButtonControlPort = 8091;

	std::string TrimCopy(const std::string& value)
	{
		size_t begin = 0;
		while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;

		size_t end = value.size();
		while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;

		return value.substr(begin, end - begin);
	}

	std::string ExtractHostFromNetworkAddress(const char *address)
	{
		std::string host = TrimCopy(address != NULL ? address : "");
		const std::string::size_type schemePos = host.find("://");
		if (schemePos != std::string::npos)
			host.erase(0, schemePos + 3);

		const std::string::size_type slashPos = host.find_first_of("/\\");
		if (slashPos != std::string::npos)
			host.erase(slashPos);

		if (host.size() > 2 && host[0] == '[')
		{
			const std::string::size_type endBracket = host.find(']');
			if (endBracket != std::string::npos)
				return host.substr(1, endBracket - 1);
		}

		const std::string::size_type portPos = host.find(':');
		if (portPos != std::string::npos)
			host.erase(portPos);

		return TrimCopy(host);
	}

	std::string ExtractHttpRequestPath(const std::string& request)
	{
		const std::string::size_type lineEnd = request.find('\n');
		std::string requestLine = request.substr(0, lineEnd == std::string::npos ? request.size() : lineEnd);
		if (!requestLine.empty() && requestLine[requestLine.size() - 1] == '\r')
			requestLine.erase(requestLine.size() - 1);

		const std::string::size_type firstSpace = requestLine.find(' ');
		if (firstSpace == std::string::npos) return "";

		const std::string method = requestLine.substr(0, firstSpace);
		if (method != "GET") return "";

		const std::string::size_type secondSpace = requestLine.find(' ', firstSpace + 1);
		if (secondSpace == std::string::npos) return "";

		std::string path = requestLine.substr(firstSpace + 1, secondSpace - firstSpace - 1);
		if (path.find("http://") == 0 || path.find("https://") == 0)
		{
			const std::string::size_type schemePos = path.find("://");
			const std::string::size_type firstSlash = path.find('/', schemePos == std::string::npos ? 0 : schemePos + 3);
			path = firstSlash == std::string::npos ? "/" : path.substr(firstSlash);
		}

		const std::string::size_type queryPos = path.find('?');
		if (queryPos != std::string::npos)
			path.erase(queryPos);

		return path;
	}

#ifdef _WIN32
	class WsaSession
	{
	private:
		bool ready;

	public:
		WsaSession() : ready(false)
		{
			WSADATA data;
			ready = WSAStartup(MAKEWORD(2, 2), &data) == 0;
		}

		~WsaSession()
		{
			if (ready) WSACleanup();
		}

		bool IsReady() const { return ready; }
	};

	void CloseSocket(SOCKET socketHandle)
	{
		if (socketHandle != INVALID_SOCKET)
			closesocket(socketHandle);
	}

	bool SendAll(SOCKET socketHandle, const std::string& payload)
	{
		const char *data = payload.c_str();
		int remaining = static_cast<int>(payload.size());
		while (remaining > 0)
		{
			const int sent = send(socketHandle, data, remaining, 0);
			if (sent == SOCKET_ERROR || sent == 0)
				return false;

			data += sent;
			remaining -= sent;
		}

		return true;
	}

	void SendJsonResponse(SOCKET socketHandle, int statusCode, const char *statusText, const std::string& json)
	{
		std::ostringstream response;
		response << "HTTP/1.1 " << statusCode << " " << statusText << "\r\n"
				 << "Content-Type: application/json\r\n"
				 << "Content-Length: " << json.size() << "\r\n"
				 << "Connection: close\r\n"
				 << "\r\n"
				 << json;
		SendAll(socketHandle, response.str());
	}

	bool ConnectWithTimeout(SOCKET socketHandle, const struct sockaddr *address, int addressLength, int timeoutMs)
	{
		u_long nonBlocking = 1;
		if (ioctlsocket(socketHandle, FIONBIO, &nonBlocking) != 0)
			return false;

		const int connectResult = connect(socketHandle, address, addressLength);
		if (connectResult == SOCKET_ERROR)
		{
			const int error = WSAGetLastError();
			if (error != WSAEWOULDBLOCK && error != WSAEINPROGRESS && error != WSAEINVAL)
			{
				u_long blocking = 0;
				ioctlsocket(socketHandle, FIONBIO, &blocking);
				return false;
			}

			fd_set writeSet;
			fd_set errorSet;
			FD_ZERO(&writeSet);
			FD_ZERO(&errorSet);
			FD_SET(socketHandle, &writeSet);
			FD_SET(socketHandle, &errorSet);

			timeval timeout;
			timeout.tv_sec = timeoutMs / 1000;
			timeout.tv_usec = (timeoutMs % 1000) * 1000;

			const int selectResult = select(0, NULL, &writeSet, &errorSet, &timeout);
			if (selectResult <= 0 || FD_ISSET(socketHandle, &errorSet))
			{
				u_long blocking = 0;
				ioctlsocket(socketHandle, FIONBIO, &blocking);
				return false;
			}

			int socketError = 0;
			int socketErrorLength = sizeof(socketError);
			if (getsockopt(socketHandle, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&socketError), &socketErrorLength) != 0 ||
				socketError != 0)
			{
				u_long blocking = 0;
				ioctlsocket(socketHandle, FIONBIO, &blocking);
				return false;
			}
		}

		u_long blocking = 0;
		ioctlsocket(socketHandle, FIONBIO, &blocking);
		return true;
	}

	bool RegisterRemoteControlClientWithPi(const char *networkAddress, int pcPort, std::string& status)
	{
		const std::string host = ExtractHostFromNetworkAddress(networkAddress);
		if (host.empty())
		{
			status = "empty Raspberry Pi address";
			return false;
		}

		WsaSession wsa;
		if (!wsa.IsReady())
		{
			status = "WinSock startup failed";
			return false;
		}

		addrinfo hints;
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;

		std::ostringstream portText;
		portText << kRaspberryButtonControlPort;

		addrinfo *result = NULL;
		if (getaddrinfo(host.c_str(), portText.str().c_str(), &hints, &result) != 0 || result == NULL)
		{
			status = "cannot resolve Raspberry Pi address";
			return false;
		}

		SOCKET socketHandle = INVALID_SOCKET;
		for (addrinfo *ptr = result; ptr != NULL; ptr = ptr->ai_next)
		{
			socketHandle = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
			if (socketHandle == INVALID_SOCKET)
				continue;

			DWORD timeoutMs = 1500;
			setsockopt(socketHandle, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
			setsockopt(socketHandle, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));

			if (ConnectWithTimeout(socketHandle, ptr->ai_addr, static_cast<int>(ptr->ai_addrlen), 1500))
				break;

			CloseSocket(socketHandle);
			socketHandle = INVALID_SOCKET;
		}

		freeaddrinfo(result);

		if (socketHandle == INVALID_SOCKET)
		{
			status = "Raspberry Pi button service is not reachable on port 8091";
			return false;
		}

		std::ostringstream body;
		body << "{\"port\":" << pcPort << ",\"protocol\":\"http\"}";

		std::ostringstream request;
		request << "POST /register HTTP/1.1\r\n"
				<< "Host: " << host << "\r\n"
				<< "Content-Type: application/json\r\n"
				<< "Content-Length: " << body.str().size() << "\r\n"
				<< "Connection: close\r\n"
				<< "\r\n"
				<< body.str();

		if (!SendAll(socketHandle, request.str()))
		{
			CloseSocket(socketHandle);
			status = "registration request failed";
			return false;
		}

		char buffer[512];
		const int received = recv(socketHandle, buffer, sizeof(buffer) - 1, 0);
		CloseSocket(socketHandle);

		if (received <= 0)
		{
			status = "Raspberry Pi did not answer registration";
			return false;
		}

		buffer[received] = '\0';
		const std::string response(buffer);
		if (response.find(" 200 ") == std::string::npos && response.find(" 204 ") == std::string::npos)
		{
			status = "Raspberry Pi rejected registration";
			return false;
		}

		status = "PC button control registered on Raspberry Pi.";
		return true;
	}
#else
	bool RegisterRemoteControlClientWithPi(const char*, int, std::string& status)
	{
		status = "remote button registration is implemented for Windows builds";
		return false;
	}
#endif

#ifdef COMPILE_WITH_RealSense2
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

#endif

	std::string BuildTimestampedMeshName()
	{
		std::time_t now = std::time(NULL);
		std::tm localTime;
#if defined(_WIN32)
		localtime_s(&localTime, &now);
#else
		localtime_r(&now, &localTime);
#endif
		char buffer[64];
		std::strftime(buffer, sizeof(buffer), "mesh_%Y%m%d_%H%M%S.obj", &localTime);
		return std::string(buffer);
	}

	void BeginLabeledRow(const char *label)
	{
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(label);
		ImGui::TableSetColumnIndex(1);
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
	}

	bool BeginSettingsTable(const char *id)
	{
		if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp)) return false;
		ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, 118.0f);
		ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
		return true;
	}

	Vector4f AspectFitRect(const Vector4f& region, const Vector2i& imageDims, const Vector2i& windowSize)
	{
		if (imageDims.x <= 0 || imageDims.y <= 0 || windowSize.x <= 0 || windowSize.y <= 0)
			return region;

		const float regionWpx = (region.z - region.x) * static_cast<float>(windowSize.x);
		const float regionHpx = (region.w - region.y) * static_cast<float>(windowSize.y);
		if (regionWpx <= 0.0f || regionHpx <= 0.0f) return region;

		const float imageAspect = static_cast<float>(imageDims.x) / static_cast<float>(imageDims.y);
		const float regionAspect = regionWpx / regionHpx;
		Vector4f out = region;

		if (regionAspect > imageAspect)
		{
			const float drawWpx = regionHpx * imageAspect;
			const float padX = ((regionWpx - drawWpx) * 0.5f) / static_cast<float>(windowSize.x);
			out.x += padX;
			out.z -= padX;
		}
		else
		{
			const float drawHpx = regionWpx / imageAspect;
			const float padY = ((regionHpx - drawHpx) * 0.5f) / static_cast<float>(windowSize.y);
			out.y += padY;
			out.w -= padY;
		}

		return out;
	}

	Vector2i ComputeStableCpuProcessingSize(const Vector2i& sourceSize)
	{
		const int maxProcessingWidth = 640;
		if (sourceSize.x <= maxProcessingWidth || sourceSize.y <= 0)
			return sourceSize;

		int height = static_cast<int>(std::floor((static_cast<float>(sourceSize.y) * maxProcessingWidth / sourceSize.x) + 0.5f));
		if (height < 1) height = 1;
		if ((height % 2) != 0) ++height;
		return Vector2i(maxProcessingWidth, height);
	}

	class CpuStableImageSourceEngine : public ImageSourceEngine
	{
	private:
		ImageSourceEngine *source;
		ITMRGBDCalib scaledCalib;
		Vector2i sourceDepthSize;
		Vector2i sourceRgbSize;
		Vector2i targetSize;
		ITMUChar4Image *sourceRgbImage;
		ITMShortImage *sourceDepthImage;

		static void AverageDepthToTarget(ITMShortImage *target, const ITMShortImage *source)
		{
			const Vector2i srcSize = source->noDims;
			const Vector2i dstSize = target->noDims;
			const short *src = source->GetData(MEMORYDEVICE_CPU);
			short *dst = target->GetData(MEMORYDEVICE_CPU);

			for (int y = 0; y < dstSize.y; ++y)
			{
				int y0 = static_cast<int>(std::floor(static_cast<float>(y) * srcSize.y / dstSize.y));
				int y1 = static_cast<int>(std::floor(static_cast<float>(y + 1) * srcSize.y / dstSize.y));
				if (y1 <= y0) y1 = y0 + 1;
				y0 = std::max(0, std::min(y0, srcSize.y - 1));
				y1 = std::max(y0 + 1, std::min(y1, srcSize.y));

				for (int x = 0; x < dstSize.x; ++x)
				{
					int x0 = static_cast<int>(std::floor(static_cast<float>(x) * srcSize.x / dstSize.x));
					int x1 = static_cast<int>(std::floor(static_cast<float>(x + 1) * srcSize.x / dstSize.x));
					if (x1 <= x0) x1 = x0 + 1;
					x0 = std::max(0, std::min(x0, srcSize.x - 1));
					x1 = std::max(x0 + 1, std::min(x1, srcSize.x));

					int sum = 0;
					int count = 0;
					for (int sy = y0; sy < y1; ++sy)
					{
						for (int sx = x0; sx < x1; ++sx)
						{
							const short depth = src[sy * srcSize.x + sx];
							if (depth > 0)
							{
								sum += depth;
								++count;
							}
						}
					}

					dst[y * dstSize.x + x] = count > 0 ? static_cast<short>((sum + count / 2) / count) : 0;
				}
			}
		}

		static void SampleRgbToTarget(ITMUChar4Image *target, const ITMUChar4Image *source)
		{
			const Vector2i srcSize = source->noDims;
			const Vector2i dstSize = target->noDims;
			const Vector4u *src = source->GetData(MEMORYDEVICE_CPU);
			Vector4u *dst = target->GetData(MEMORYDEVICE_CPU);

			for (int y = 0; y < dstSize.y; ++y)
			{
				const int sy = std::max(0, std::min(static_cast<int>((static_cast<float>(y) + 0.5f) * srcSize.y / dstSize.y), srcSize.y - 1));
				for (int x = 0; x < dstSize.x; ++x)
				{
					const int sx = std::max(0, std::min(static_cast<int>((static_cast<float>(x) + 0.5f) * srcSize.x / dstSize.x), srcSize.x - 1));
					dst[y * dstSize.x + x] = src[sy * srcSize.x + sx];
				}
			}
		}

	public:
		CpuStableImageSourceEngine(ImageSourceEngine *_source, const Vector2i& _targetSize)
			: source(_source), sourceDepthSize(_source->getDepthImageSize()), sourceRgbSize(_source->getRGBImageSize()),
			  targetSize(_targetSize), sourceRgbImage(NULL), sourceDepthImage(NULL)
		{
			scaledCalib = source->getCalib();
			scaledCalib.intrinsics_d = scaledCalib.intrinsics_d.MakeRescaled(sourceDepthSize, targetSize);
			scaledCalib.intrinsics_rgb = scaledCalib.intrinsics_rgb.MakeRescaled(sourceRgbSize, targetSize);

			sourceRgbImage = new ITMUChar4Image(sourceRgbSize, true, false);
			sourceDepthImage = new ITMShortImage(sourceDepthSize, true, false);
		}

		~CpuStableImageSourceEngine()
		{
			delete sourceRgbImage;
			delete sourceDepthImage;
			delete source;
		}

		ITMRGBDCalib getCalib() const { return scaledCalib; }
		Vector2i getDepthImageSize(void) const { return targetSize; }
		Vector2i getRGBImageSize(void) const { return targetSize; }
		bool hasMoreImages(void) const { return source != NULL && source->hasMoreImages(); }
		bool hasImagesNow(void) const { return source != NULL && source->hasImagesNow(); }
		bool hasPipelineError(void) const { return source != NULL && source->hasPipelineError(); }

		void getImages(ITMUChar4Image *rgb, ITMShortImage *rawDepth)
		{
			source->getImages(sourceRgbImage, sourceDepthImage);
			SampleRgbToTarget(rgb, sourceRgbImage);
			AverageDepthToTarget(rawDepth, sourceDepthImage);
		}
	};

	ImageSourceEngine *WrapForStableCpuProcessing(ImageSourceEngine *source)
	{
		if (source == NULL) return NULL;
		const Vector2i sourceSize = source->getDepthImageSize();
		const Vector2i targetSize = ComputeStableCpuProcessingSize(sourceSize);
		if (targetSize == sourceSize) return source;

		std::cout << "CPU stable processing: camera "
				  << sourceSize.x << "x" << sourceSize.y
				  << " -> InfiniTAM " << targetSize.x << "x" << targetSize.y << std::endl;
		return new CpuStableImageSourceEngine(source, targetSize);
	}
}

void UIEngine::glutDisplayFunction()
{
	UIEngine *uiEngine = UIEngine::Instance();

	// ITMBasicEngine::view is NULL until the first ProcessFrame() call.
	// Calling GetImage (especially SCENERAYCAST) on an uninitialised engine causes
	// an access violation (Windows SEH) that is not caught by C++ try/catch.
	// Guard: skip all GetImage calls until at least one frame has been processed.
	if (uiEngine->processedFrameNo > 0)
	{
		// After many consecutive bad tracking frames the ICP pose can diverge to NaN.
		// GetImage(SCENERAYCAST) then does NaN→int voxel-index conversions which are
		// undefined behaviour on x86 and cause an access violation (Windows SEH crash).
		// Skip the 3-D raycast only — depth and RGB windows still update normally.
		const bool rayCastSafe = (uiEngine->consecutiveBadTrackingFrames < 150);

		try {
			if (rayCastSafe)
				uiEngine->mainEngine->GetImage(uiEngine->outImage[0], uiEngine->outImageType[0], &uiEngine->freeviewPose, &uiEngine->freeviewIntrinsics);
			for (int w = 1; w < NUM_WIN; w++) uiEngine->mainEngine->GetImage(uiEngine->outImage[w], uiEngine->outImageType[w]);
		} catch (const std::exception& e) {
			uiEngine->lastStatusMessage = std::string("Render error: ") + e.what();
			uiEngine->needsRefresh = true;
		} catch (...) {
			uiEngine->lastStatusMessage = "Render error: unknown exception.";
			uiEngine->needsRefresh = true;
		}
	}

	glClear(GL_COLOR_BUFFER_BIT);
	glColor3f(1.0f, 1.0f, 1.0f);
	glEnable(GL_TEXTURE_2D);

	ITMUChar4Image** showImgs = uiEngine->outImage;
	Vector4f *winReg = uiEngine->winReg;
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	{
		glLoadIdentity();
		glOrtho(0.0, 1.0, 0.0, 1.0, 0.0, 1.0);

		glMatrixMode(GL_MODELVIEW);
		glPushMatrix();
		{
			glEnable(GL_TEXTURE_2D);
			for (int w = 0; w < NUM_WIN; w++) {
				if (uiEngine->outImageType[w] == ITMMainEngine::InfiniTAM_IMAGE_UNKNOWN) continue;
				glBindTexture(GL_TEXTURE_2D, uiEngine->textureId[w]);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, showImgs[w]->noDims.x, showImgs[w]->noDims.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, showImgs[w]->GetData(MEMORYDEVICE_CPU));
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				const Vector4f drawReg = AspectFitRect(winReg[w], showImgs[w]->noDims, uiEngine->winSize);
				glBegin(GL_QUADS); {
					glTexCoord2f(0, 1); glVertex2f(drawReg.x, drawReg.y);
					glTexCoord2f(1, 1); glVertex2f(drawReg.z, drawReg.y);
					glTexCoord2f(1, 0); glVertex2f(drawReg.z, drawReg.w);
					glTexCoord2f(0, 0); glVertex2f(drawReg.x, drawReg.w);
				}
				glEnd();
			}
			glDisable(GL_TEXTURE_2D);
		}
		glPopMatrix();
	}
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();

	uiEngine->DrawScanAreaOverlay();

	switch (uiEngine->trackingResult)
	{
	case 0: glColor3f(1.0f, 0.0f, 0.0f); break;
	case 1: glColor3f(1.0f, 1.0f, 0.0f); break;
	case 2: glColor3f(0.0f, 1.0f, 0.0f); break;
	default: glColor3f(1.0f, 1.0f, 1.0f); break;
	}

	glRasterPos2f(0.85f, -0.962f);

	char str[200]; sprintf(str, "%04.2lf", uiEngine->processedTime);
	safe_glutBitmapString(GLUT_BITMAP_HELVETICA_18, (const char*)str);


	ImGui_ImplOpenGL3_NewFrame();
	ImGuiGlutBridge::NewFrame();
	ImGui::NewFrame();
	uiEngine->RenderControlPanel();
	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

	glutSwapBuffers();
	uiEngine->needsRefresh = false;
}

void UIEngine::glutIdleFunction()
{
	UIEngine *uiEngine = UIEngine::Instance();
	uiEngine->ProcessRemoteControlCommands();
	uiEngine->TickCameraReinit();

	// Detect background model save completion and report status.
	if (!uiEngine->modelSavePending.load() && uiEngine->modelSaveThread.joinable())
	{
		uiEngine->modelSaveThread.join();
		uiEngine->lastStatusMessage = "Model saved: " + uiEngine->modelSaveFileName;
		uiEngine->needsRefresh = true;
	}

	switch (uiEngine->mainLoopAction)
	{
	case PROCESS_VIDEO:
		if (uiEngine->ProcessFrame())
		{
			uiEngine->processedFrameNo++;
			uiEngine->needsRefresh = true;
		}
		break;
	case EXIT:
#ifdef FREEGLUT
		glutLeaveMainLoop();
#else
		exit(0);
#endif
		break;
	case PROCESS_PAUSED:
	default:
		break;
	}

	if (uiEngine->imguiKeyboardCaptured || uiEngine->WantsKeyboardCapture())
		uiEngine->needsRefresh = true;

	if (uiEngine->needsRefresh) {
		glutPostRedisplay();
	}
}

void UIEngine::glutReshapeFunction(int width, int height)
{
	UIEngine *uiEngine = UIEngine::Instance();

	uiEngine->winSize.x = std::max(1, width);
	uiEngine->winSize.y = std::max(1, height);
	glViewport(0, 0, uiEngine->winSize.x, uiEngine->winSize.y);
	uiEngine->needsRefresh = true;
	glutPostRedisplay();
}

void UIEngine::glutKeyDownFunction(unsigned char key, int x, int y)
{
	UIEngine *uiEngine = UIEngine::Instance();

	ImGuiGlutBridge::KeyboardDown(key, x, y);
	uiEngine->imguiKeyboardCaptured = uiEngine->WantsKeyboardCapture() || uiEngine->IsControlPanelHit(x, y);
	uiEngine->needsRefresh = true;
	glutPostRedisplay();
}

void UIEngine::glutKeyUpFunction(unsigned char key, int x, int y)
{
	UIEngine *uiEngine = UIEngine::Instance();

	const bool wasCapturedByImGui = uiEngine->imguiKeyboardCaptured || uiEngine->WantsKeyboardCapture();
	ImGuiGlutBridge::KeyboardUp(key, x, y);
	uiEngine->imguiKeyboardCaptured = wasCapturedByImGui && uiEngine->WantsKeyboardCapture();
	uiEngine->needsRefresh = true;
	glutPostRedisplay();
}

void UIEngine::glutMouseButtonFunction(int button, int state, int x, int y)
{
	UIEngine *uiEngine = UIEngine::Instance();

	ImGuiGlutBridge::MouseButton(button, state, x, y);
	if (uiEngine->WantsMouseCapture() || uiEngine->IsControlPanelHit(x, y))
	{
		uiEngine->needsRefresh = true;
		glutPostRedisplay();
		return;
	}

	if (state == GLUT_DOWN)
	{
		switch (button)
		{
		case GLUT_LEFT_BUTTON: uiEngine->mouseState = 1; break;
		case GLUT_MIDDLE_BUTTON: uiEngine->mouseState = 3; break;
		case GLUT_RIGHT_BUTTON: uiEngine->mouseState = 2; break;
		default: break;
		}
		uiEngine->mouseLastClick.x = x;
		uiEngine->mouseLastClick.y = y;

		glutSetCursor(GLUT_CURSOR_NONE);
	}
	else if (state == GLUT_UP && !uiEngine->mouseWarped)
	{
		uiEngine->mouseState = 0;
		glutSetCursor(GLUT_CURSOR_INHERIT);
	}
}

static inline Matrix3f createRotation(const Vector3f & _axis, float angle)
{
	Vector3f axis = normalize(_axis);
	float si = sinf(angle);
	float co = cosf(angle);

	Matrix3f ret;
	ret.setIdentity();

	ret *= co;
	for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) ret.at(c, r) += (1.0f - co) * axis[c] * axis[r];

	Matrix3f skewmat;
	skewmat.setZeros();
	skewmat.at(1, 0) = -axis.z;
	skewmat.at(0, 1) = axis.z;
	skewmat.at(2, 0) = axis.y;
	skewmat.at(0, 2) = -axis.y;
	skewmat.at(2, 1) = axis.x;
	skewmat.at(1, 2) = -axis.x;
	skewmat *= si;
	ret += skewmat;

	return ret;
}

void UIEngine::glutMouseMoveFunction(int x, int y)
{
	UIEngine *uiEngine = UIEngine::Instance();

	ImGuiGlutBridge::MouseMove(x, y);
	if (uiEngine->WantsMouseCapture() || uiEngine->IsControlPanelHit(x, y))
	{
		uiEngine->needsRefresh = true;
		glutPostRedisplay();
		return;
	}

	if (uiEngine->mouseWarped)
	{
		uiEngine->mouseWarped = false;
		return;
	}

	if (!uiEngine->freeviewActive || uiEngine->mouseState == 0) return;

	Vector2i movement;
	movement.x = x - uiEngine->mouseLastClick.x;
	movement.y = y - uiEngine->mouseLastClick.y;

	Vector2i realWinSize(glutGet(GLUT_WINDOW_WIDTH), glutGet(GLUT_WINDOW_HEIGHT));
	Vector2i activeWinTopLeft(20, 20);
	Vector2i activeWinBottomRight(realWinSize.width - 20, realWinSize.height - 20);
	Vector2i activeWinSize(realWinSize.width - 40, realWinSize.height - 40);

	bool warpNeeded = false;

	if (x < activeWinTopLeft.x)
	{
		x += activeWinSize.x;
		warpNeeded = true;
	}
	else if (x >= activeWinBottomRight.x)
	{
		x -= activeWinSize.x;
		warpNeeded = true;
	}

	if (y < activeWinTopLeft.y)
	{
		y += activeWinSize.y;
		warpNeeded = true;
	}
	else if (y >= activeWinBottomRight.y)
	{
		y -= activeWinSize.y;
		warpNeeded = true;
	}

	if (warpNeeded)
	{
		glutWarpPointer(x, y);
		uiEngine->mouseWarped = true;
	}

	uiEngine->mouseLastClick.x = x;
	uiEngine->mouseLastClick.y = y;

	if ((movement.x == 0) && (movement.y == 0)) return;

	static const float scale_rotation = 0.005f;
	static const float scale_translation = 0.0025f;

	switch (uiEngine->mouseState)
	{
	case 1:
	{
		Vector3f axis((float)-movement.y, (float)-movement.x, 0.0f);
		float angle = scale_rotation * sqrt((float)(movement.x * movement.x + movement.y*movement.y));
		Matrix3f rot = createRotation(axis, angle);
		uiEngine->freeviewPose.SetRT(rot * uiEngine->freeviewPose.GetR(), rot * uiEngine->freeviewPose.GetT());
		uiEngine->freeviewPose.Coerce();
		uiEngine->needsRefresh = true;
		break;
	}
	case 2:
	{
		uiEngine->freeviewPose.SetT(uiEngine->freeviewPose.GetT() + scale_translation * Vector3f((float)movement.x, (float)movement.y, 0.0f));
		uiEngine->needsRefresh = true;
		break;
	}
	case 3:
	{
		uiEngine->freeviewPose.SetT(uiEngine->freeviewPose.GetT() + scale_translation * Vector3f(0.0f, 0.0f, (float)movement.y));
		uiEngine->needsRefresh = true;
		break;
	}
	default: break;
	}
}

void UIEngine::glutMouseWheelFunction(int button, int dir, int x, int y)
{
	UIEngine *uiEngine = UIEngine::Instance();

	ImGuiGlutBridge::MouseWheel(dir, x, y);
	if (uiEngine->WantsMouseCapture() || uiEngine->IsControlPanelHit(x, y))
	{
		uiEngine->needsRefresh = true;
		glutPostRedisplay();
		return;
	}

	static const float scale_translation = 0.05f;

	uiEngine->freeviewPose.SetT(uiEngine->freeviewPose.GetT() + scale_translation * Vector3f(0.0f, 0.0f, (dir > 0) ? -1.0f : 1.0f));
	uiEngine->needsRefresh = true;
}

void UIEngine::AllocateImages(ITMLibSettings::DeviceType deviceType)
{
	const Vector2i depthSize = imageSource->getDepthImageSize();
	const Vector2i rgbSize = imageSource->getRGBImageSize();

	if (winSize.x <= 0 || winSize.y <= 0)
		winSize = Vector2i(1280, 720);

	winReg[0] = Vector4f(0.0f, 0.0f, 0.665f, 1.0f);
	winReg[1] = Vector4f(0.665f, 0.5f, 1.0f, 1.0f);
	winReg[2] = Vector4f(0.665f, 0.0f, 1.0f, 0.5f);

	bool allocateGPU = false;
	if (deviceType == ITMLibSettings::DEVICE_CUDA) allocateGPU = true;

	for (int w = 0; w < NUM_WIN; w++)
		outImage[w] = new ITMUChar4Image(depthSize, true, allocateGPU);

	inputRGBImage = new ITMUChar4Image(rgbSize, true, allocateGPU);
	inputRawDepthImage = new ITMShortImage(depthSize, true, allocateGPU);
	inputIMUMeasurement = new ITMIMUMeasurement();

	saveImage = new ITMUChar4Image(depthSize, true, false);

	outImageType[0] = ITMMainEngine::InfiniTAM_IMAGE_SCENERAYCAST;
	outImageType[1] = ITMMainEngine::InfiniTAM_IMAGE_ORIGINAL_DEPTH;
	outImageType[2] = ITMMainEngine::InfiniTAM_IMAGE_ORIGINAL_RGB;
	if (inputRGBImage->noDims == Vector2i(0, 0)) outImageType[2] = ITMMainEngine::InfiniTAM_IMAGE_UNKNOWN;
}

void UIEngine::ReleaseImages()
{
	for (int w = 0; w < NUM_WIN; w++)
	{
		delete outImage[w];
		outImage[w] = NULL;
	}

	delete inputRGBImage; inputRGBImage = NULL;
	delete inputRawDepthImage; inputRawDepthImage = NULL;
	delete inputIMUMeasurement; inputIMUMeasurement = NULL;
	delete saveImage; saveImage = NULL;
}

ITMMainEngine *UIEngine::CreateMainEngineForImageSource(ImageSourceEngine *source) const
{
	switch (engineSettings->libMode)
	{
	case ITMLibSettings::LIBMODE_BASIC:
		return new ITMBasicEngine<ITMVoxel, ITMVoxelIndex>(engineSettings, source->getCalib(), source->getRGBImageSize(), source->getDepthImageSize());
	case ITMLibSettings::LIBMODE_BASIC_SURFELS:
		return new ITMBasicSurfelEngine<ITMSurfelT>(engineSettings, source->getCalib(), source->getRGBImageSize(), source->getDepthImageSize());
	case ITMLibSettings::LIBMODE_LOOPCLOSURE:
		return new ITMMultiEngine<ITMVoxel, ITMVoxelIndex>(engineSettings, source->getCalib(), source->getRGBImageSize(), source->getDepthImageSize());
	default:
		throw std::runtime_error("Unsupported library mode!");
	}
}

void UIEngine::ReplaceImageSource(ImageSourceEngine *newImageSource, IMUSourceEngine *newIMUSource)
{
	ApplyLivePerformanceSettings();
	ITMMainEngine *newMainEngine = CreateMainEngineForImageSource(newImageSource);

	mainLoopAction = PROCESS_PAUSED;
	if (rgbVideoWriter != NULL) { delete rgbVideoWriter; rgbVideoWriter = NULL; }
	if (depthVideoWriter != NULL) { delete depthVideoWriter; depthVideoWriter = NULL; }
	isRecording = false;

	ReleaseImages();

	if (ownsEngineObjects)
	{
		// Run the 512 MB TSDF destruction on a background thread to keep GLUT live.
		ITMMainEngine *oldMainEngine = mainEngine;
		std::thread([oldMainEngine]() { delete oldMainEngine; }).detach();
		delete imageSource;
		if (imuSource != NULL) delete imuSource;
	}

	imageSource = newImageSource;
	imuSource = newIMUSource;
	mainEngine = newMainEngine;
	ownsEngineObjects = true;

	AllocateImages(engineSettings->deviceType);

	freeviewActive = false;
	integrationActive = true;
	currentColourMode = 0;
	mouseState = 0;
	mouseWarped = false;
	currentFrameNo = 0;
	processedFrameNo = 0;
	processedTime = 0.0f;
	trackingResult = -1;
	consecutiveNoFrames = 0;
	consecutivePipelineErrors = 0;
	consecutiveBadTrackingFrames = 0;
	consecutiveGoodTrackingFrames = 0;
	cameraReinitState = CameraReinitState::IDLE;
	cameraReinitAttempt = 0;
	// Set far in the past so reconnect is allowed immediately on first use.
	cameraLastReconnectTime = std::chrono::steady_clock::now() - std::chrono::seconds(30);
	lastGoodFrameTime = std::chrono::steady_clock::now();
	ResetAdaptiveCpuLoad();
	needsRefresh = true;
}

void UIEngine::ApplyLivePerformanceSettings()
{
	if (engineSettings == NULL) return;

	ITMLibSettings *settings = const_cast<ITMLibSettings*>(engineSettings);
	settings->useApproximateRaycast = scanPerformanceState.approximateRaycast;
	settings->useBilateralFilter = scanPerformanceState.useBilateralFilter;
	settings->trackerConfig = scanPerformanceState.useColourTracking
		? kColourDepthTrackerConfig
		: kDepthOnlyTrackerConfig;
	settings->libMode = ITMLibSettings::LIBMODE_BASIC;
	settings->behaviourOnFailure = scanPerformanceState.protectTrackingLoss
		? ITMLibSettings::FAILUREMODE_STOP_INTEGRATION
		: ITMLibSettings::FAILUREMODE_IGNORE;

	const int meshDetailIndex = std::max(0, std::min(scanPerformanceState.meshDetailIndex, 2));
	settings->sceneParams.voxelSize = kMeshVoxelSizesM[meshDetailIndex];
	settings->sceneParams.mu = kMeshVoxelSizesM[meshDetailIndex] * 4.0f;

	const float maxDepthM = std::max(0.30f, scanPerformanceState.maxDepthCm * 0.01f);
	settings->sceneParams.viewFrustum_max = std::max(settings->sceneParams.viewFrustum_min + 0.05f, maxDepthM);
}

int UIEngine::GetFrameStride() const
{
	const int index = std::max(0, std::min(scanPerformanceState.frameStrideIndex, 3));
	const int manualStride = kFrameStrides[index];
	if (!scanPerformanceState.useAdaptiveCpuLoad)
		return manualStride;

	return std::max(manualStride, std::max(1, std::min(adaptiveFrameStride, 4)));
}

void UIEngine::ResetAdaptiveCpuLoad()
{
	adaptiveFrameStride = 1;
	adaptiveSlowFrameCount = 0;
	adaptiveFastFrameCount = 0;
	adaptiveLastFrameMs = 0.0f;
}

void UIEngine::UpdateAdaptiveCpuLoad(float frameMs)
{
	adaptiveLastFrameMs = frameMs;
	if (!scanPerformanceState.useAdaptiveCpuLoad || frameMs <= 0.0f || processedFrameNo < 8)
		return;

	const float slowFrameMs = 45.0f;
	const float verySlowFrameMs = 80.0f;
	const float fastFrameMs = 25.0f;

	if (frameMs >= verySlowFrameMs)
		adaptiveSlowFrameCount += 3;
	else if (frameMs >= slowFrameMs)
		adaptiveSlowFrameCount++;
	else if (adaptiveSlowFrameCount > 0)
		adaptiveSlowFrameCount--;

	if (frameMs <= fastFrameMs)
		adaptiveFastFrameCount++;
	else
		adaptiveFastFrameCount = 0;

	if (adaptiveSlowFrameCount >= 6 && adaptiveFrameStride < 4)
	{
		adaptiveFrameStride++;
		adaptiveSlowFrameCount = 0;
		adaptiveFastFrameCount = 0;

		std::ostringstream message;
		message << "Auto speed: processing every " << GetFrameStride() << " frames for stable high-resolution scanning.";
		lastStatusMessage = message.str();
		needsRefresh = true;
	}
	else if (adaptiveFastFrameCount >= 90 && adaptiveFrameStride > 1)
	{
		adaptiveFrameStride--;
		adaptiveSlowFrameCount = 0;
		adaptiveFastFrameCount = 0;

		std::ostringstream message;
		message << "Auto speed: processing every " << GetFrameStride() << " frames.";
		lastStatusMessage = message.str();
		needsRefresh = true;
	}
}

float UIEngine::GetScanAreaSizeM() const
{
	const int index = std::max(0, std::min(scanPerformanceState.scanAreaIndex, 3));
	return kScanAreaSizesM[index];
}

float UIEngine::GetDepthScaleM() const
{
	if (imageSource == NULL) return 0.001f;

	const ITMDisparityCalib& disparityCalib = imageSource->getCalib().disparityCalib;
	if (disparityCalib.GetType() == ITMDisparityCalib::TRAFO_AFFINE)
		return disparityCalib.GetParams().x;

	return 0.001f;
}

void UIEngine::ApplyScanAreaMask(ITMShortImage *rawDepthImage)
{
	if (rawDepthImage == NULL || imageSource == NULL) return;

	const Vector2i dims = rawDepthImage->noDims;
	if (dims.x <= 0 || dims.y <= 0) return;

	const ITMIntrinsics& intrinsics = imageSource->getCalib().intrinsics_d;
	const float fx = intrinsics.projectionParamsSimple.fx;
	const float fy = intrinsics.projectionParamsSimple.fy;
	const float px = intrinsics.projectionParamsSimple.px;
	const float py = intrinsics.projectionParamsSimple.py;
	if (fx == 0.0f || fy == 0.0f) return;

	const ITMDisparityCalib& disparityCalib = imageSource->getCalib().disparityCalib;
	const Vector2f depthCalibParams = disparityCalib.GetParams();
	const float scale = GetDepthScaleM();
	const float offset = disparityCalib.GetType() == ITMDisparityCalib::TRAFO_AFFINE ? depthCalibParams.y : 0.0f;
	const float maxDepthM = std::max(0.30f, scanPerformanceState.maxDepthCm * 0.01f);
	const float halfAreaM = GetScanAreaSizeM() * 0.5f;
	const bool restrictArea = scanPerformanceState.restrictScanArea;

	short *depth = rawDepthImage->GetData(MEMORYDEVICE_CPU);
	for (int y = 0; y < dims.y; ++y)
	{
		for (int x = 0; x < dims.x; ++x)
		{
			const int idx = y * dims.x + x;
			const short raw = depth[idx];
			if (raw <= 0)
			{
				depth[idx] = 0;
				continue;
			}

			const float z = static_cast<float>(raw) * scale + offset;
			if (z <= 0.0f || z > maxDepthM)
			{
				depth[idx] = 0;
				continue;
			}

			if (restrictArea)
			{
				const float cameraX = (static_cast<float>(x) - px) * z / fx;
				const float cameraY = (static_cast<float>(y) - py) * z / fy;
				if (std::fabs(cameraX) > halfAreaM || std::fabs(cameraY) > halfAreaM)
					depth[idx] = 0;
			}
		}
	}
}

void UIEngine::DrawScanAreaOverlay()
{
	if (!scanPerformanceState.showScanAreaOverlay || !scanPerformanceState.restrictScanArea || imageSource == NULL)
		return;

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0.0, 1.0, 0.0, 1.0, -1.0, 1.0);

	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();

	glDisable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glLineWidth(2.0f);

	DrawScanAreaOverlayForWindow(1, imageSource->getCalib().intrinsics_d);
	if (outImageType[2] != ITMMainEngine::InfiniTAM_IMAGE_UNKNOWN)
		DrawScanAreaOverlayForWindow(2, imageSource->getCalib().intrinsics_rgb);

	glLineWidth(1.0f);
	glDisable(GL_BLEND);

	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
}

void UIEngine::DrawScanAreaOverlayForWindow(int windowIndex, const ITMIntrinsics& intrinsics) const
{
	if (windowIndex < 0 || windowIndex >= NUM_WIN || outImage[windowIndex] == NULL) return;

	const Vector2i dims = outImage[windowIndex]->noDims;
	if (dims.x <= 0 || dims.y <= 0) return;

	const float guideDepthM = std::max(0.10f, scanPerformanceState.guideDepthCm * 0.01f);
	const float sizeM = GetScanAreaSizeM();
	const float fx = intrinsics.projectionParamsSimple.fx;
	const float fy = intrinsics.projectionParamsSimple.fy;
	const float px = intrinsics.projectionParamsSimple.px;
	const float py = intrinsics.projectionParamsSimple.py;
	if (fx == 0.0f || fy == 0.0f) return;

	const float halfW = 0.5f * sizeM * fx / guideDepthM;
	const float halfH = 0.5f * sizeM * fy / guideDepthM;
	const float leftPx = std::max(0.0f, px - halfW);
	const float rightPx = std::min(static_cast<float>(dims.x - 1), px + halfW);
	const float topPx = std::max(0.0f, py - halfH);
	const float bottomPx = std::min(static_cast<float>(dims.y - 1), py + halfH);

	if (rightPx <= leftPx || bottomPx <= topPx) return;

	const Vector4f r = AspectFitRect(winReg[windowIndex], dims, winSize);
	const float winW = r.z - r.x;
	const float winH = r.w - r.y;
	const float left = r.x + (leftPx / static_cast<float>(dims.x)) * winW;
	const float right = r.x + (rightPx / static_cast<float>(dims.x)) * winW;
	const float top = r.w - (topPx / static_cast<float>(dims.y)) * winH;
	const float bottom = r.w - (bottomPx / static_cast<float>(dims.y)) * winH;

	glColor4f(0.0f, 0.80f, 1.0f, 0.35f);
	glBegin(GL_QUADS);
	glVertex2f(left, bottom);
	glVertex2f(right, bottom);
	glVertex2f(right, top);
	glVertex2f(left, top);
	glEnd();

	glColor4f(0.0f, 0.95f, 1.0f, 0.95f);
	glBegin(GL_LINE_LOOP);
	glVertex2f(left, bottom);
	glVertex2f(right, bottom);
	glVertex2f(right, top);
	glVertex2f(left, top);
	glEnd();
}

void UIEngine::ApplyEngineIntegrationState(bool active)
{
	ITMBasicEngine<ITMVoxel, ITMVoxelIndex> *basicEngine = dynamic_cast<ITMBasicEngine<ITMVoxel, ITMVoxelIndex>*>(mainEngine);
	if (basicEngine != NULL)
	{
		if (active) basicEngine->turnOnIntegration();
		else basicEngine->turnOffIntegration();
	}

	ITMBasicSurfelEngine<ITMSurfelT> *basicSurfelEngine = dynamic_cast<ITMBasicSurfelEngine<ITMSurfelT>*>(mainEngine);
	if (basicSurfelEngine != NULL)
	{
		if (active) basicSurfelEngine->turnOnIntegration();
		else basicSurfelEngine->turnOffIntegration();
	}

	ITMMultiEngine<ITMVoxel, ITMVoxelIndex> *multiEngine = dynamic_cast<ITMMultiEngine<ITMVoxel, ITMVoxelIndex>*>(mainEngine);
	if (multiEngine != NULL)
	{
		if (active) multiEngine->turnOnIntegration();
		else multiEngine->turnOffIntegration();
	}
}

void UIEngine::SetIntegrationActive(bool active)
{
	integrationActive = active;
	ApplyEngineIntegrationState(integrationActive);
}

void UIEngine::ApplyScanControlCommand(ScanControlCommand command, bool remoteCommand)
{
	const bool canScan = mainEngine != NULL && d415GuiState.cameraInitialised;

	if (!canScan)
	{
		if (remoteCommand)
		{
			switch (command)
			{
			case SCAN_CONTROL_START: lastStatusMessage = "Remote START ignored: camera is not initialised."; break;
			case SCAN_CONTROL_PAUSE: lastStatusMessage = "Remote PAUSE ignored: camera is not initialised."; break;
			case SCAN_CONTROL_STOP: lastStatusMessage = "Remote STOP ignored: camera is not initialised."; break;
			case SCAN_CONTROL_RESET: lastStatusMessage = "Remote RESET ignored: camera is not initialised."; break;
			}
			needsRefresh = true;
		}
		return;
	}

	switch (command)
	{
	case SCAN_CONTROL_START:
		SetIntegrationActive(true);
		mainLoopAction = PROCESS_VIDEO;
		lastStatusMessage = remoteCommand ? "Remote START: scanning." : "Scanning.";
		needsRefresh = true;
		break;

	case SCAN_CONTROL_PAUSE:
		SetIntegrationActive(false);
		mainLoopAction = PROCESS_VIDEO;
		lastStatusMessage = remoteCommand
			? "Remote PAUSE: integration paused; camera and tracking continue."
			: "Integration paused; camera and tracking continue.";
		needsRefresh = true;
		break;

	case SCAN_CONTROL_STOP:
		SetIntegrationActive(false);
		mainLoopAction = PROCESS_PAUSED;
		lastStatusMessage = remoteCommand
			? "Remote STOP: camera stays connected and frames are not processed."
			: "Stopped; camera stays connected and frames are not processed.";
		needsRefresh = true;
		break;

	case SCAN_CONTROL_RESET:
		ResetScene();
		if (remoteCommand && lastStatusMessage.find("New scan:") == 0)
			lastStatusMessage = "Remote RESET: reconstruction preview and tracking were reset.";
		break;
	}
}

void UIEngine::StartRemoteControlServer()
{
	if (remoteControlRunning.load() || remoteControlThread.joinable())
		return;

	remoteControlRunning.store(true);
	try
	{
		remoteControlThread = std::thread(&UIEngine::RemoteControlThreadMain, this);
	}
	catch (const std::exception& e)
	{
		remoteControlRunning.store(false);
		lastStatusMessage = std::string("Remote button control failed to start: ") + e.what();
		needsRefresh = true;
	}
}

void UIEngine::StopRemoteControlServer()
{
	remoteControlRunning.store(false);
	if (remoteControlThread.joinable())
		remoteControlThread.join();
}

void UIEngine::QueueRemoteControlCommand(ScanControlCommand command)
{
	std::lock_guard<std::mutex> lock(remoteControlMutex);
	pendingRemoteControlCommands.push_back(command);
}

void UIEngine::ProcessRemoteControlCommands()
{
	std::vector<ScanControlCommand> commands;
	{
		std::lock_guard<std::mutex> lock(remoteControlMutex);
		commands.swap(pendingRemoteControlCommands);
	}

	for (size_t i = 0; i < commands.size(); ++i)
		ApplyScanControlCommand(commands[i], true);
}

void UIEngine::RemoteControlThreadMain()
{
#ifdef _WIN32
	WsaSession wsa;
	if (!wsa.IsReady())
	{
		std::cout << "Mirage remote control: WinSock startup failed." << std::endl;
		remoteControlRunning.store(false);
		return;
	}

	SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listenSocket == INVALID_SOCKET)
	{
		std::cout << "Mirage remote control: socket creation failed." << std::endl;
		remoteControlRunning.store(false);
		return;
	}

	BOOL reuseAddress = TRUE;
	setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuseAddress), sizeof(reuseAddress));

	sockaddr_in service;
	memset(&service, 0, sizeof(service));
	service.sin_family = AF_INET;
	service.sin_addr.s_addr = htonl(INADDR_ANY);
	service.sin_port = htons(static_cast<u_short>(remoteControlPort));

	if (bind(listenSocket, reinterpret_cast<SOCKADDR*>(&service), sizeof(service)) == SOCKET_ERROR)
	{
		std::cout << "Mirage remote control: port " << remoteControlPort << " is not available." << std::endl;
		CloseSocket(listenSocket);
		remoteControlRunning.store(false);
		return;
	}

	if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR)
	{
		std::cout << "Mirage remote control: listen failed." << std::endl;
		CloseSocket(listenSocket);
		remoteControlRunning.store(false);
		return;
	}

	std::cout << "Mirage remote control listening on port " << remoteControlPort << "." << std::endl;

	while (remoteControlRunning.load())
	{
		fd_set readSet;
		FD_ZERO(&readSet);
		FD_SET(listenSocket, &readSet);

		timeval timeout;
		timeout.tv_sec = 0;
		timeout.tv_usec = 250000;

		const int selectResult = select(0, &readSet, NULL, NULL, &timeout);
		if (!remoteControlRunning.load()) break;
		if (selectResult == 0) continue;
		if (selectResult == SOCKET_ERROR) break;

		SOCKET clientSocket = accept(listenSocket, NULL, NULL);
		if (clientSocket == INVALID_SOCKET)
			continue;

		DWORD timeoutMs = 1000;
		setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
		setsockopt(clientSocket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));

		std::string request;
		char buffer[1024];
		while (request.size() < 4096 && request.find("\r\n\r\n") == std::string::npos)
		{
			const int received = recv(clientSocket, buffer, sizeof(buffer), 0);
			if (received <= 0) break;
			request.append(buffer, received);
			if (received < static_cast<int>(sizeof(buffer))) break;
		}

		const std::string path = ExtractHttpRequestPath(request);
		if (path == "/start")
		{
			QueueRemoteControlCommand(SCAN_CONTROL_START);
			SendJsonResponse(clientSocket, 200, "OK", "{\"ok\":true,\"command\":\"start\"}");
		}
		else if (path == "/stop")
		{
			QueueRemoteControlCommand(SCAN_CONTROL_STOP);
			SendJsonResponse(clientSocket, 200, "OK", "{\"ok\":true,\"command\":\"stop\"}");
		}
		else if (path == "/reset")
		{
			QueueRemoteControlCommand(SCAN_CONTROL_RESET);
			SendJsonResponse(clientSocket, 200, "OK", "{\"ok\":true,\"command\":\"reset\"}");
		}
		else if (path == "/" || path == "/status")
		{
			SendJsonResponse(clientSocket, 200, "OK", "{\"ok\":true,\"service\":\"mirage_pc_control\"}");
		}
		else
		{
			SendJsonResponse(clientSocket, 404, "Not Found", "{\"ok\":false,\"error\":\"unknown command\"}");
		}

		shutdown(clientSocket, SD_BOTH);
		CloseSocket(clientSocket);
	}

	CloseSocket(listenSocket);
#else
	std::cout << "Mirage remote control is implemented for Windows builds." << std::endl;
	remoteControlRunning.store(false);
#endif
}

void UIEngine::ToggleFreeview()
{
	currentColourMode = 0;
	if (freeviewActive)
	{
		outImageType[0] = ITMMainEngine::InfiniTAM_IMAGE_SCENERAYCAST;
		outImageType[1] = ITMMainEngine::InfiniTAM_IMAGE_ORIGINAL_DEPTH;
		freeviewActive = false;
		lastStatusMessage = "Camera-follow view.";
	}
	else
	{
		outImageType[0] = ITMMainEngine::InfiniTAM_IMAGE_FREECAMERA_SHADED;
		outImageType[1] = ITMMainEngine::InfiniTAM_IMAGE_SCENERAYCAST;

		freeviewPose.SetFrom(mainEngine->GetTrackingState()->pose_d);
		if (mainEngine->GetView() != NULL)
		{
			freeviewIntrinsics = mainEngine->GetView()->calib.intrinsics_d;
			outImage[0]->ChangeDims(mainEngine->GetView()->depth->noDims);
		}

		ITMMultiEngine<ITMVoxel, ITMVoxelIndex> *multiEngine = dynamic_cast<ITMMultiEngine<ITMVoxel, ITMVoxelIndex>*>(mainEngine);
		if (multiEngine != NULL)
		{
			int idx = multiEngine->findPrimaryLocalMapIdx();
			if (idx < 0) idx = 0;
			multiEngine->setFreeviewLocalMapIdx(idx);
		}

		freeviewActive = true;
		lastStatusMessage = "Free viewpoint enabled.";
	}

	needsRefresh = true;
}

void UIEngine::ResetScene()
{
	if (mainEngine == NULL || imageSource == NULL)
	{
		lastStatusMessage = "Reset failed: camera is not initialised.";
		needsRefresh = true;
		return;
	}

	const bool wasRunning = mainLoopAction == PROCESS_VIDEO;
	mainLoopAction = PROCESS_PAUSED;

	try
	{
		ITMMainEngine *oldMainEngine = mainEngine;
		ITMMainEngine *newMainEngine = CreateMainEngineForImageSource(imageSource);
		mainEngine = newMainEngine;

		// ~ITMBasicEngine releases 512 MB of TSDF + render state. On a dense scan
		// this can block the main thread for >1 s. Run it detached so GLUT stays live.
		std::thread([oldMainEngine]() { delete oldMainEngine; }).detach();

		freeviewActive = false;
		currentColourMode = 0;
		outImageType[0] = ITMMainEngine::InfiniTAM_IMAGE_SCENERAYCAST;
		outImageType[1] = ITMMainEngine::InfiniTAM_IMAGE_ORIGINAL_DEPTH;
		outImageType[2] = (inputRGBImage == NULL || inputRGBImage->noDims == Vector2i(0, 0))
			? ITMMainEngine::InfiniTAM_IMAGE_UNKNOWN
			: ITMMainEngine::InfiniTAM_IMAGE_ORIGINAL_RGB;

		for (int w = 0; w < NUM_WIN; ++w)
			if (outImage[w] != NULL) outImage[w]->Clear();

		processedFrameNo = 0;
		currentFrameNo = 0;
		processedTime = 0.0f;
		trackingResult = -1;
		consecutiveNoFrames = 0;
		consecutivePipelineErrors = 0;
		consecutiveBadTrackingFrames = 0;
		consecutiveGoodTrackingFrames = 0;
		lastIntegratedPoseValid = false;
		ResetAdaptiveCpuLoad();
		SetIntegrationActive(true);
		mainLoopAction = wasRunning ? PROCESS_VIDEO : PROCESS_PAUSED;
		lastStatusMessage = "New scan: reconstruction preview and tracking were reset.";
	}
	catch (const std::exception &e)
	{
		mainLoopAction = wasRunning ? PROCESS_VIDEO : PROCESS_PAUSED;
		lastStatusMessage = std::string("Reset failed: ") + e.what();
	}

	needsRefresh = true;
}

void UIEngine::SaveSceneState()
{
	try
	{
		mainEngine->SaveToFile();
		lastStatusMessage = "Scene state saved.";
	}
	catch (const std::runtime_error &e)
	{
		lastStatusMessage = std::string("Scene save failed: ") + e.what();
	}
	needsRefresh = true;
}

void UIEngine::SaveModel()
{
	if (modelSavePending.load()) return; // already saving

	modelSaveFileName = BuildTimestampedMeshName();
	lastStatusMessage = "Saving model: " + modelSaveFileName + " ...";
	needsRefresh = true;
	modelSavePending.store(true);

	if (modelSaveThread.joinable()) modelSaveThread.join();

	// Run mesh export on a background thread so the GLUT main thread keeps
	// draining RS2 frames. Without this, MeshScene + OBJ write can block the
	// main thread for 2-10 seconds, causing the D415 USB firmware watchdog to
	// fire a hardware reset ("USB device not recognized").
	// ProcessFrame() skips TSDF writes while modelSavePending is true to avoid
	// a data race between the background mesh read and foreground TSDF writes.
	modelSaveThread = std::thread([this]() {
		mainEngine->SaveSceneToMesh(modelSaveFileName.c_str());
		modelSavePending.store(false);
	});
}

void UIEngine::ApplyAndInitialiseD415()
{
	mainLoopAction = PROCESS_PAUSED;
	d415GuiState.applyingCamera = true;
	lastStatusMessage = "Initialising RealSense D415...";
	needsRefresh = true;

#ifdef COMPILE_WITH_RealSense2
	const bool useNetwork = d415GuiState.connectionModeIndex == 1;
	try
	{
		const Vector2i resolution = kResolutions[d415GuiState.resolutionIndex];

		if (d415GuiState.cameraInitialised)
		{
			ImageSourceEngine *blankSource = new BlankImageGenerator(calibFilename.c_str(), resolution);
			ReplaceImageSource(blankSource, NULL);
			d415GuiState.cameraInitialised = false;
			// Let the firmware finish its USB reset before we re-open the device.
			std::this_thread::sleep_for(std::chrono::milliseconds(600));
		}

		std::string sensorStatus;
		ImageSourceEngine *newSource = NULL;

		if (useNetwork)
		{
#ifdef COMPILE_WITH_RealSense2Net
			if (d415GuiState.networkAddress[0] == '\0')
			{
				lastStatusMessage = "Network RealSense IP address is empty.";
				d415GuiState.applyingCamera = false;
				return;
			}

			sensorStatus = "Network RealSense connected. Preset/laser are skipped in network mode.";
			newSource = new RealSense2NetworkEngine(calibFilename.c_str(), d415GuiState.networkAddress,
				true, resolution, resolution, d415GuiState.presetIndex, d415GuiState.laserPower);
#else
			lastStatusMessage = "Application was built without librealsense2 network support.";
			d415GuiState.applyingCamera = false;
			return;
#endif
		}
		else
		{
			// Retry creating the entire RealSense2Engine (context + device + pipeline) when
			// 0x8007001f is returned — the USB firmware sometimes needs several seconds after
			// a previous session ends or right after OS enumeration on fresh launch.
			const int rs2Preset = ToRealSenseVisualPreset(d415GuiState.presetIndex);
			int retries = 5;
			while (true)
			{
				try
				{
					newSource = new RealSense2Engine(calibFilename.c_str(), true, resolution, resolution,
						rs2Preset, d415GuiState.laserPower);
					sensorStatus = "RealSense depth options applied.";
					break;
				}
#ifdef COMPILE_WITH_RealSense2
				catch (const rs2::error&)
#else
				catch (const std::exception&)
#endif
				{
					if (--retries == 0) throw;
					lastStatusMessage = std::string("RealSense init failed, retrying (")
						+ std::to_string(5 - retries) + "/4)...";
					needsRefresh = true;
					std::this_thread::sleep_for(std::chrono::milliseconds(1200));
				}
			}
		}

		if (newSource->getDepthImageSize().x == 0)
		{
			delete newSource;
			lastStatusMessage = useNetwork
				? "Network RealSense stream did not start. Check Network IP and Raspberry Pi connection."
				: "USB RealSense stream did not start. No D415 is connected to this PC or USB mode is selected.";
			d415GuiState.applyingCamera = false;
			return;
		}

		const Vector2i cameraResolution = newSource->getDepthImageSize();
	if (this->scanPerformanceState.useStableCpuResolution) {
		newSource = WrapForStableCpuProcessing(newSource);
	}
	const Vector2i processingResolution = newSource->getDepthImageSize();

		cameraLastReconnectTime = std::chrono::steady_clock::now();
		lastGoodFrameTime = std::chrono::steady_clock::now();
		lastIntegratedPoseValid = false;
		ReplaceImageSource(newSource, NULL);
		d415GuiState.cameraInitialised = true;

		std::ostringstream status;
		const int meshDetailIndex = std::max(0, std::min(scanPerformanceState.meshDetailIndex, 2));
		status << sensorStatus << " "
			   << (useNetwork ? "Network " : "USB ")
			   << "streaming " << cameraResolution.x << "x" << cameraResolution.y;
		if (processingResolution != cameraResolution)
			status << ", CPU processing " << processingResolution.x << "x" << processingResolution.y;
		status
			   << ", max depth " << scanPerformanceState.maxDepthCm << " cm"
			   << ", voxel " << static_cast<int>(kMeshVoxelSizesM[meshDetailIndex] * 1000.0f + 0.5f) << " mm.";

		if (useNetwork)
		{
			std::string registrationStatus;
			if (RegisterRemoteControlClientWithPi(d415GuiState.networkAddress, remoteControlPort, registrationStatus))
				status << " Remote buttons registered.";
			else
				status << " Remote buttons not registered: " << registrationStatus << ".";
		}

		lastStatusMessage = status.str();
	}
	catch (const std::exception &e)
	{
		const std::string error = e.what();
		if (useNetwork && error.find("liveMedia") != std::string::npos)
		{
			lastStatusMessage = std::string("Network RealSense init failed. RTSP port 8554 is not reachable or rs-server is not streaming. Details: ") + error;
		}
		else
		{
			lastStatusMessage = std::string("RealSense init failed: ") + error;
		}
	}
#else
	lastStatusMessage = "Application was built without librealsense2 support.";
#endif

	d415GuiState.applyingCamera = false;
	needsRefresh = true;
}

// Reconnects the camera pipeline without destroying mainEngine (scan data preserved).
// Called automatically when the pipeline stops delivering frames mid-scan.
void UIEngine::AutoReinitCamera()
{
	if (!d415GuiState.cameraInitialised) return;
	if (d415GuiState.connectionModeIndex == 1) return;

	// Tear down the stalled pipeline.
	// ~RealSense2Engine() runs pipe->stop() in a detached thread — returns immediately.
	if (ownsEngineObjects && imageSource != NULL)
	{
		delete imageSource;
		imageSource = NULL;
		ownsEngineObjects = false;
	}

	// Kick off the non-blocking state machine. TickCameraReinit() will advance it
	// each idle frame: DRAINING (2 s USB settle) → CONNECTING (up to 5 attempts).
	cameraReinitState = CameraReinitState::DRAINING;
	cameraReinitTime  = std::chrono::steady_clock::now();
	cameraReinitAttempt = 0;
	lastStatusMessage = "Camera disconnected - reconnecting...";
	needsRefresh = true;
}

void UIEngine::TickCameraReinit()
{
	if (cameraReinitState == CameraReinitState::IDLE) return;

#ifdef COMPILE_WITH_RealSense2
	using namespace std::chrono;
	const auto now = steady_clock::now();

	if (cameraReinitState == CameraReinitState::DRAINING)
	{
		const long long elapsedMs = duration_cast<milliseconds>(now - cameraReinitTime).count();
		// Prefer to wait for the background pipe->stop() thread before opening a new pipeline.
		// But if it is still running after 5 s it is probably deadlocked — proceed anyway
		// rather than blocking reconnect forever.
		const bool stopThreadDone = (InputSource::RealSense2Engine::activePipeStopCount.load() == 0);
		if (!stopThreadDone && elapsedMs < 5000) return;
		// Also enforce a minimum USB settle time regardless.
		if (elapsedMs < 1200) return;
		cameraReinitState = CameraReinitState::CONNECTING;
		cameraReinitTime  = now - milliseconds(800); // first attempt fires immediately
		cameraReinitAttempt = 0;
	}

	if (cameraReinitState == CameraReinitState::CONNECTING)
	{
		// 800 ms between attempts: USB enumeration + librealsense open takes ~200-500 ms.
		if (duration_cast<milliseconds>(now - cameraReinitTime).count() < 800)
			return;

		cameraReinitAttempt++;
		cameraReinitTime = now;

		const Vector2i resolution = kResolutions[d415GuiState.resolutionIndex];
		const int rs2Preset = ToRealSenseVisualPreset(d415GuiState.presetIndex);

		ImageSourceEngine *newSource = NULL;
		try
		{
			newSource = new RealSense2Engine(calibFilename.c_str(), true, resolution, resolution,
				rs2Preset, d415GuiState.laserPower);
		}
		catch (const rs2::error&) {}

		if (newSource == NULL || newSource->getDepthImageSize().x == 0)
		{
			delete newSource;
			if (cameraReinitAttempt >= 5)
			{
				d415GuiState.cameraInitialised = false;
				cameraReinitState = CameraReinitState::IDLE;
				lastStatusMessage = "Auto reconnect failed after 5 attempts. Press Apply & Initialize Camera.";
			}
			else
			{
				lastStatusMessage = std::string("Auto reconnect attempt ")
					+ std::to_string(cameraReinitAttempt) + "/5...";
			}
			needsRefresh = true;
			return;
		}

		if (scanPerformanceState.useStableCpuResolution)
			newSource = WrapForStableCpuProcessing(newSource);

		imageSource = newSource;
		ownsEngineObjects = true;
		// Grace period: start the error counter at -15 so the camera needs to produce
		// 45 bad cycles (vs. the normal 30) before the next auto-reconnect triggers.
		// Prevents a rapid reconnect loop when USB is marginal.
		consecutiveNoFrames = -15;
		consecutivePipelineErrors = 0;
		consecutiveBadTrackingFrames = 0;
		consecutiveGoodTrackingFrames = 0;
		cameraLastReconnectTime = std::chrono::steady_clock::now();
		lastGoodFrameTime = std::chrono::steady_clock::now();
		lastIntegratedPoseValid = false;
		cameraReinitState = CameraReinitState::IDLE;
		// Re-enable integration in case it was paused by tracking loss before disconnect.
		SetIntegrationActive(true);
		mainLoopAction = PROCESS_VIDEO;
		lastStatusMessage = "Camera reconnected automatically. Scanning resumed.";
		needsRefresh = true;
	}
#endif
}

void UIEngine::RenderControlPanel()
{
	const ImGuiIO& io = ImGui::GetIO();
	ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(330.0f, io.DisplaySize.y > 40.0f ? io.DisplaySize.y - 20.0f : 360.0f), ImGuiCond_Always);

	ImGui::Begin("Scan Control", NULL,
		ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

	ImGui::TextUnformatted("Camera D415");
	if (BeginSettingsTable("camera_settings"))
	{
		BeginLabeledRow("Connection");
		if (ImGui::RadioButton("USB", d415GuiState.connectionModeIndex == 0))
			d415GuiState.connectionModeIndex = 0;
		ImGui::SameLine();
		if (ImGui::RadioButton("Network", d415GuiState.connectionModeIndex == 1))
			d415GuiState.connectionModeIndex = 1;

		if (d415GuiState.connectionModeIndex == 1)
		{
			BeginLabeledRow("Network IP");
			ImGui::InputText("##NetworkAddress", d415GuiState.networkAddress,
				static_cast<size_t>(sizeof(d415GuiState.networkAddress)));
		}

		BeginLabeledRow("Resolution");
		ImGui::Combo("##Resolution", &d415GuiState.resolutionIndex, kResolutionNames, 3);

		BeginLabeledRow("Preset");
		ImGui::Combo("##VisualPreset", &d415GuiState.presetIndex, kPresetNames, 3);

		BeginLabeledRow("Laser power");
		ImGui::SliderInt("##LaserPower", &d415GuiState.laserPower, 0, 360);

		ImGui::EndTable();
	}

	if (d415GuiState.connectionModeIndex == 1)
		ImGui::TextWrapped("Network mode: Use 640x480 only. 848x480 is unsupported; 1280x720 saturates network bandwidth and will not stream reliably.");
	if (ImGui::Checkbox("CPU stable res", &this->scanPerformanceState.useStableCpuResolution)) {
		if (this->d415GuiState.cameraInitialised) {
			this->lastStatusMessage = "CPU stable resolution applies after Apply & Initialize Camera.";
		}
	}

	if (this->scanPerformanceState.useStableCpuResolution) {
		ImGui::TextWrapped("CPU mode: high camera resolutions are internally processed at 640 px width for stable tracking.");
	}
	else {
		ImGui::TextWrapped("CPU mode: full selected resolution is processed; high resolutions can lag on CPU.");
	}

	if (d415GuiState.applyingCamera) ImGui::BeginDisabled();
	if (ImGui::Button("Apply & Initialize Camera", ImVec2(-1.0f, 0.0f)))
		ApplyAndInitialiseD415();
	if (d415GuiState.applyingCamera) ImGui::EndDisabled();

	ImGui::Separator();
	ImGui::TextUnformatted("Performance");
	if (BeginSettingsTable("performance_settings"))
	{
		BeginLabeledRow("Process");
		ImGui::Combo("##ProcessStride", &scanPerformanceState.frameStrideIndex, kFrameStrideNames, 4);

		BeginLabeledRow("Auto speed");
		if (ImGui::Checkbox("##AutoSpeed", &scanPerformanceState.useAdaptiveCpuLoad))
		{
			ResetAdaptiveCpuLoad();
			lastStatusMessage = scanPerformanceState.useAdaptiveCpuLoad
				? "Auto speed enabled."
				: "Auto speed disabled.";
		}

		BeginLabeledRow("Approx raycast");
		if (ImGui::Checkbox("##ApproxRaycast", &scanPerformanceState.approximateRaycast))
			ApplyLivePerformanceSettings();

		BeginLabeledRow("Depth filter");
		if (ImGui::Checkbox("##BilateralFilter", &scanPerformanceState.useBilateralFilter))
			ApplyLivePerformanceSettings();

		BeginLabeledRow("Max depth");
		if (ImGui::SliderInt("##MaxDepth", &scanPerformanceState.maxDepthCm, 30, 300, "%d cm"))
			ApplyLivePerformanceSettings();

		ImGui::EndTable();
	}
	if (scanPerformanceState.useAdaptiveCpuLoad)
		ImGui::Text("Auto speed: every %d, %.1f ms", GetFrameStride(), adaptiveLastFrameMs);

	ImGui::Separator();
	ImGui::TextUnformatted("Reconstruction");
	if (BeginSettingsTable("reconstruction_settings"))
	{
		BeginLabeledRow("Mesh detail");
		if (ImGui::Combo("##MeshDetail", &scanPerformanceState.meshDetailIndex, kMeshDetailNames, 3))
		{
			ApplyLivePerformanceSettings();
			if (d415GuiState.cameraInitialised)
				lastStatusMessage = "Mesh detail applies after Apply & Initialize Camera.";
		}

		BeginLabeledRow("Protect loss");
		if (ImGui::Checkbox("##ProtectTrackingLoss", &scanPerformanceState.protectTrackingLoss))
			ApplyLivePerformanceSettings();

		BeginLabeledRow("Colour tracking");
		if (ImGui::Checkbox("##ColourTracking", &scanPerformanceState.useColourTracking))
		{
			ApplyLivePerformanceSettings();
			if (d415GuiState.cameraInitialised)
				lastStatusMessage = "Colour tracking applies after Apply & Initialize Camera.";
		}

		BeginLabeledRow("Progressive");
		ImGui::Checkbox("##SkipStaticFrames", &scanPerformanceState.skipStaticFrames);

		if (scanPerformanceState.skipStaticFrames)
		{
			BeginLabeledRow("Move thresh");
			ImGui::SliderFloat("##StaticThresh", &scanPerformanceState.staticThresholdMm, 1.0f, 20.0f, "%.0f mm");
		}

		ImGui::EndTable();
	}
	if (scanPerformanceState.skipStaticFrames)
		ImGui::TextWrapped("Progressive: scan builds only when you move the camera. Static areas are not re-processed.");

	ImGui::Separator();
	ImGui::TextUnformatted("Scan area");
	if (BeginSettingsTable("scan_area_settings"))
	{
		BeginLabeledRow("Limit area");
		ImGui::Checkbox("##LimitArea", &scanPerformanceState.restrictScanArea);

		BeginLabeledRow("Overlay");
		ImGui::Checkbox("##ShowOverlay", &scanPerformanceState.showScanAreaOverlay);

		BeginLabeledRow("Area");
		ImGui::Combo("##ScanArea", &scanPerformanceState.scanAreaIndex, kScanAreaNames, 4);

		BeginLabeledRow("Guide depth");
		ImGui::SliderInt("##GuideDepth", &scanPerformanceState.guideDepthCm, 20, 150, "%d cm");

		ImGui::EndTable();
	}

	ImGui::Separator();
	ImGui::TextUnformatted("Scanning");

	const bool canScan = mainEngine != NULL && d415GuiState.cameraInitialised;
	if (!canScan) ImGui::BeginDisabled();

	if (ImGui::Button("Start / Resume Scan", ImVec2(-1.0f, 0.0f)))
		ApplyScanControlCommand(SCAN_CONTROL_START, false);

	if (ImGui::Button("Pause Scan", ImVec2(-1.0f, 0.0f)))
		ApplyScanControlCommand(SCAN_CONTROL_PAUSE, false);

	if (ImGui::Button("Stop Scan", ImVec2(-1.0f, 0.0f)))
		ApplyScanControlCommand(SCAN_CONTROL_STOP, false);

	if (ImGui::Button("Reset (New Scan)", ImVec2(-1.0f, 0.0f)))
		ApplyScanControlCommand(SCAN_CONTROL_RESET, false);

	const bool isSaving = modelSavePending.load();
	if (isSaving) ImGui::BeginDisabled();
	const char* saveLabel = isSaving ? "Saving model..." : "Save Model (.obj)";
	if (ImGui::Button(saveLabel, ImVec2(-1.0f, 0.0f)))
		SaveModel();
	if (isSaving) ImGui::EndDisabled();

	if (!canScan) ImGui::EndDisabled();

	ImGui::Separator();
	ImGui::TextUnformatted("View / Scene");
	if (BeginSettingsTable("view_scene_settings"))
	{
		BeginLabeledRow("Viewpoint");
		if (ImGui::Button(freeviewActive ? "Follow Camera" : "Free Viewpoint", ImVec2(-1.0f, 0.0f)))
			ToggleFreeview();

		BeginLabeledRow("Colour");
		std::vector<UIColourMode>& colourModes = freeviewActive ? colourModes_freeview : colourModes_main;
		if (!colourModes.empty())
		{
			if (currentColourMode < 0 || currentColourMode >= static_cast<int>(colourModes.size()))
				currentColourMode = 0;

			const char *currentModeName = colourModes[currentColourMode].name;
			if (ImGui::BeginCombo("##ViewColourMode", currentModeName))
			{
				for (int i = 0; i < static_cast<int>(colourModes.size()); ++i)
				{
					const bool selected = currentColourMode == i;
					if (ImGui::Selectable(colourModes[i].name, selected))
					{
						currentColourMode = i;
						outImageType[0] = colourModes[i].type;
						needsRefresh = true;
					}
					if (selected) ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
		}

		ImGui::EndTable();
	}

	if (ImGui::Button("Exit", ImVec2(-1.0f, 0.0f)))
		mainLoopAction = EXIT;

	ImGui::Separator();
	ImGui::Text("Frames: %d", processedFrameNo);
	if (!lastStatusMessage.empty()) ImGui::TextWrapped("%s", lastStatusMessage.c_str());

	ImGui::End();
}

bool UIEngine::IsControlPanelHit(int x, int y) const
{
	const int panelLeft = 10;
	const int panelRight = 340;
	const int panelTop = 10;
	const int panelBottom = glutGet(GLUT_WINDOW_HEIGHT) - 10;
	return x >= panelLeft && x <= panelRight && y >= panelTop && y <= panelBottom;
}

bool UIEngine::WantsMouseCapture() const
{
	return ImGui::GetCurrentContext() != NULL && ImGui::GetIO().WantCaptureMouse;
}

bool UIEngine::WantsKeyboardCapture() const
{
	return ImGui::GetCurrentContext() != NULL && ImGui::GetIO().WantCaptureKeyboard;
}

void UIEngine::Initialise(int & argc, char** argv, ImageSourceEngine *imageSource, IMUSourceEngine *imuSource, ITMMainEngine *mainEngine,
	const char *outFolder, const ITMLibSettings *internalSettings, const char *calibFilename)
{
	this->freeviewActive = false;
	this->integrationActive = true;
	this->currentColourMode = 0;
	this->colourModes_main.push_back(UIColourMode("shaded greyscale", ITMMainEngine::InfiniTAM_IMAGE_SCENERAYCAST));
	this->colourModes_main.push_back(UIColourMode("integrated colours", ITMMainEngine::InfiniTAM_IMAGE_COLOUR_FROM_VOLUME));
	this->colourModes_main.push_back(UIColourMode("surface normals", ITMMainEngine::InfiniTAM_IMAGE_COLOUR_FROM_NORMAL));
	this->colourModes_main.push_back(UIColourMode("confidence", ITMMainEngine::InfiniTAM_IMAGE_COLOUR_FROM_CONFIDENCE));
	this->colourModes_freeview.push_back(UIColourMode("shaded greyscale", ITMMainEngine::InfiniTAM_IMAGE_FREECAMERA_SHADED));
	this->colourModes_freeview.push_back(UIColourMode("integrated colours", ITMMainEngine::InfiniTAM_IMAGE_FREECAMERA_COLOUR_FROM_VOLUME));
	this->colourModes_freeview.push_back(UIColourMode("surface normals", ITMMainEngine::InfiniTAM_IMAGE_FREECAMERA_COLOUR_FROM_NORMAL));
	this->colourModes_freeview.push_back(UIColourMode("confidence", ITMMainEngine::InfiniTAM_IMAGE_FREECAMERA_COLOUR_FROM_CONFIDENCE));

	this->imageSource = imageSource;
	this->imuSource = imuSource;
	this->mainEngine = mainEngine;
	this->engineSettings = internalSettings;
	this->ownsEngineObjects = true;
	this->calibFilename = calibFilename != NULL ? calibFilename : "";
	this->lastStatusMessage = "";
	this->remoteControlPort = kRemoteControlDefaultPort;
	this->remoteControlRunning.store(false);
	this->pendingRemoteControlCommands.clear();
	this->d415GuiState.connectionModeIndex = 1;
	strncpy(this->d415GuiState.networkAddress, "192.168.1.10", sizeof(this->d415GuiState.networkAddress));
	this->d415GuiState.networkAddress[sizeof(this->d415GuiState.networkAddress) - 1] = '\0';
	this->d415GuiState.resolutionIndex = 2;
	this->d415GuiState.presetIndex = 2;
	this->d415GuiState.laserPower = 150;
	this->d415GuiState.cameraInitialised = imageSource != NULL && imageSource->getDepthImageSize().x > 0 &&
		dynamic_cast<BlankImageGenerator*>(imageSource) == NULL;
	this->d415GuiState.applyingCamera = false;
	this->scanPerformanceState.frameStrideIndex = 0;
	this->scanPerformanceState.scanAreaIndex = 2;
	this->scanPerformanceState.meshDetailIndex = 1;
	this->scanPerformanceState.maxDepthCm = 100;
	this->scanPerformanceState.guideDepthCm = 60;
	this->scanPerformanceState.restrictScanArea = false;
	this->scanPerformanceState.showScanAreaOverlay = true;
	this->scanPerformanceState.useStableCpuResolution = true;
	this->scanPerformanceState.useAdaptiveCpuLoad = true;
	this->scanPerformanceState.approximateRaycast = true;
	this->scanPerformanceState.useBilateralFilter = false;
	this->scanPerformanceState.protectTrackingLoss = true;
	this->scanPerformanceState.useColourTracking = false;
	this->scanPerformanceState.skipStaticFrames = true;
	this->scanPerformanceState.staticThresholdMm = 5.0f;
	this->lastIntegratedPoseValid = false;
	ApplyLivePerformanceSettings();

	for (int w = 0; w < NUM_WIN; w++) outImage[w] = NULL;
	inputRGBImage = NULL;
	inputRawDepthImage = NULL;
	inputIMUMeasurement = NULL;
	saveImage = NULL;

	{
		size_t len = strlen(outFolder);
		this->outFolder = new char[len + 1];
		strcpy(this->outFolder, outFolder);
	}

	this->isRecording = false;
	this->currentFrameNo = 0;
	this->consecutiveNoFrames = 0;
	this->consecutivePipelineErrors = 0;
	ResetAdaptiveCpuLoad();
	this->rgbVideoWriter = NULL;
	this->depthVideoWriter = NULL;

	AllocateImages(internalSettings->deviceType);

	glutInit(&argc, argv);
	glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE);
	glutInitWindowSize(winSize.x, winSize.y);
	glutCreateWindow("InfiniTAM");
	glGenTextures(NUM_WIN, textureId);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGuiGlutBridge::Init();
	ImGui_ImplOpenGL3_Init("#version 120");

	glutDisplayFunc(UIEngine::glutDisplayFunction);
	glutReshapeFunc(UIEngine::glutReshapeFunction);
	glutKeyboardFunc(UIEngine::glutKeyDownFunction);
	glutKeyboardUpFunc(UIEngine::glutKeyUpFunction);
	glutMouseFunc(UIEngine::glutMouseButtonFunction);
	glutMotionFunc(UIEngine::glutMouseMoveFunction);
	glutPassiveMotionFunc(UIEngine::glutMouseMoveFunction);
	glutIdleFunc(UIEngine::glutIdleFunction);

#ifdef FREEGLUT
	glutMouseWheelFunc(UIEngine::glutMouseWheelFunction);
	glutSetOption(GLUT_ACTION_ON_WINDOW_CLOSE, 1);
#endif

	glutFullScreen();

	mainLoopAction = PROCESS_PAUSED;
	mouseState = 0;
	mouseWarped = false;
	imguiKeyboardCaptured = false;
	needsRefresh = false;
	processedFrameNo = 0;
	processedTime = 0.0f;
	trackingResult = -1;

#ifndef COMPILE_WITHOUT_CUDA
	ORcudaSafeCall(cudaThreadSynchronize());
#endif

	sdkCreateTimer(&timer_instant);
	sdkCreateTimer(&timer_average);

	sdkResetTimer(&timer_average);
	StartRemoteControlServer();

	printf("initialised.\n");
}

void UIEngine::SaveScreenshot(const char *filename) const
{
	ITMUChar4Image screenshot(getWindowSize(), true, false);
	GetScreenshot(&screenshot);
	SaveImageToFile(&screenshot, filename, true);
}

void UIEngine::GetScreenshot(ITMUChar4Image *dest) const
{
	glReadPixels(0, 0, dest->noDims.x, dest->noDims.y, GL_RGBA, GL_UNSIGNED_BYTE, dest->GetData(MEMORYDEVICE_CPU));
}

bool UIEngine::ProcessFrame()
{
	if (!d415GuiState.cameraInitialised || imageSource == NULL || dynamic_cast<BlankImageGenerator*>(imageSource) != NULL)
	{
		mainLoopAction = PROCESS_PAUSED;
		// Don't overwrite "reconnecting..." message while TickCameraReinit is active.
		if (cameraReinitState == CameraReinitState::IDLE)
		{
			lastStatusMessage = "Camera is not initialised. Use Apply & Initialize Camera first.";
			needsRefresh = true;
		}
		return false;
	}

	if (!imageSource->hasMoreImages()) return false;
	try {
		imageSource->getImages(inputRGBImage, inputRawDepthImage);
	} catch (const std::exception& e) {
		mainLoopAction = PROCESS_PAUSED;
		lastStatusMessage = std::string("Stream error: ") + e.what();
		needsRefresh = true;
		return false;
	} catch (...) {
		mainLoopAction = PROCESS_PAUSED;
		lastStatusMessage = "Stream error: unknown exception in getImages.";
		needsRefresh = true;
		return false;
	}

	// hasImagesNow() reflects whether getImages() received a real frame this cycle.
	if (!imageSource->hasImagesNow())
	{
		const int increment = imageSource->hasPipelineError() ? 3 : 1;
		consecutiveNoFrames += increment;

		using Clock = std::chrono::steady_clock;
		const auto now = Clock::now();
		const bool reconnectCooldownOk =
			std::chrono::duration_cast<std::chrono::milliseconds>(
				now - cameraLastReconnectTime).count() > 2000;
		// Use a longer no-frame timeout during the first 12 s after camera init/reconnect.
		// At 1280x720 the D415 can take 5-8 s to start streaming; without this grace
		// period the reconnect fires before the camera is ready, causing an infinite loop.
		// After the warmup window a genuine disconnect (reader thread dies after 3 s of
		// rs2 errors) still triggers reconnect within ~4 s of the last good frame.
		const long long msSinceConnect =
			std::chrono::duration_cast<std::chrono::milliseconds>(
				now - cameraLastReconnectTime).count();
		const long long noFrameThreshMs = (msSinceConnect < 12000) ? 10000 : 4000;
		const bool noFrameForTooLong =
			std::chrono::duration_cast<std::chrono::milliseconds>(
				now - lastGoodFrameTime).count() > noFrameThreshMs;
		if (consecutiveNoFrames >= 30 && noFrameForTooLong && reconnectCooldownOk)
		{
			consecutiveNoFrames = 0;
			consecutivePipelineErrors = 0;
			lastStatusMessage = imageSource->hasPipelineError()
				? "Camera disconnected - reconnecting..."
				: "Camera stalled - reconnecting...";
			needsRefresh = true;
			AutoReinitCamera();
		}
		return false;
	}
	consecutiveNoFrames = 0;
	consecutivePipelineErrors = 0;
	lastGoodFrameTime = std::chrono::steady_clock::now();

	currentFrameNo++;
	if ((currentFrameNo % GetFrameStride()) != 0)
		return false;

	ApplyScanAreaMask(inputRawDepthImage);

	if (imuSource != NULL) {
		if (!imuSource->hasMoreMeasurements()) return false;
		else imuSource->getMeasurement(inputIMUMeasurement);
	}

	if (isRecording)
	{
		char str[250];

		sprintf(str, "%s/%04d.pgm", outFolder, currentFrameNo);
		SaveImageToFile(inputRawDepthImage, str);

		if (inputRGBImage->noDims != Vector2i(0, 0)) {
			sprintf(str, "%s/%04d.ppm", outFolder, currentFrameNo);
			SaveImageToFile(inputRGBImage, str);
		}
	}
	if ((rgbVideoWriter != NULL) && (inputRGBImage->noDims.x != 0)) {
		if (!rgbVideoWriter->isOpen()) rgbVideoWriter->open("out_rgb.avi", inputRGBImage->noDims.x, inputRGBImage->noDims.y, false, 30);
		rgbVideoWriter->writeFrame(inputRGBImage);
	}
	if ((depthVideoWriter != NULL) && (inputRawDepthImage->noDims.x != 0)) {
		if (!depthVideoWriter->isOpen()) depthVideoWriter->open("out_d.avi", inputRawDepthImage->noDims.x, inputRawDepthImage->noDims.y, true, 30);
		depthVideoWriter->writeFrame(inputRawDepthImage);
	}

	sdkResetTimer(&timer_instant);
	sdkStartTimer(&timer_instant); sdkStartTimer(&timer_average);

	ITMTrackingState::TrackingResult trackerResult = ITMTrackingState::TRACKING_POOR;
	if (modelSavePending.load())
	{
		// Background mesh export in progress: skip TSDF write to prevent a data race
		// between the save thread reading the scene and ProcessFrame writing to it.
		// RS2 frames are still drained via imageSource->getImages() above, keeping
		// the D415 USB stream alive during the save.
	}
	else
	{
		// Progressive scan (Skanect-like): skip TSDF integration when the camera
		// hasn't moved enough since the last integrated frame. Tracking still runs
		// every frame so pose estimation stays accurate. Only new viewpoints add
		// geometry to the TSDF, preventing CPU waste and noise in already-scanned areas.
		bool poseBasedSkip = false;
		if (scanPerformanceState.skipStaticFrames && integrationActive && lastIntegratedPoseValid)
		{
			const float* cp = mainEngine->GetTrackingState()->pose_d->GetParams();
			const float* lp = lastIntegratedPose.GetParams();
			const float dt2 = (cp[0]-lp[0])*(cp[0]-lp[0])
			                 + (cp[1]-lp[1])*(cp[1]-lp[1])
			                 + (cp[2]-lp[2])*(cp[2]-lp[2]);
			const float dr2 = (cp[3]-lp[3])*(cp[3]-lp[3])
			                 + (cp[4]-lp[4])*(cp[4]-lp[4])
			                 + (cp[5]-lp[5])*(cp[5]-lp[5]);
			const float threshM = scanPerformanceState.staticThresholdMm * 0.001f;
			constexpr float kRotThreshRad = 3.0f * 3.14159265f / 180.0f;
			if (dt2 < threshM * threshM && dr2 < kRotThreshRad * kRotThreshRad)
				poseBasedSkip = true;
		}
		if (poseBasedSkip) ApplyEngineIntegrationState(false);

		try {
			if (imuSource != NULL) trackerResult = mainEngine->ProcessFrame(inputRGBImage, inputRawDepthImage, inputIMUMeasurement);
			else trackerResult = mainEngine->ProcessFrame(inputRGBImage, inputRawDepthImage);
		} catch (const std::exception& e) {
			if (poseBasedSkip) ApplyEngineIntegrationState(integrationActive);
			mainLoopAction = PROCESS_PAUSED;
			lastStatusMessage = std::string("Scan error: ") + e.what();
			needsRefresh = true;
			return false;
		} catch (...) {
			if (poseBasedSkip) ApplyEngineIntegrationState(integrationActive);
			mainLoopAction = PROCESS_PAUSED;
			lastStatusMessage = "Scan error: unknown exception in engine.";
			needsRefresh = true;
			return false;
		}

		if (poseBasedSkip)
		{
			// Restore the integration flag the tracking-loss guard set.
			ApplyEngineIntegrationState(integrationActive);
		}
		else if (integrationActive && trackerResult == ITMTrackingState::TRACKING_GOOD)
		{
			// Record the pose at which we just integrated so future frames can
			// compare against it to decide whether to skip.
			lastIntegratedPose = *mainEngine->GetTrackingState()->pose_d;
			lastIntegratedPoseValid = true;
		}
	}

	trackingResult = (int)trackerResult;
	if (trackerResult == ITMTrackingState::TRACKING_GOOD)
	{
		consecutiveBadTrackingFrames = 0;
		consecutiveGoodTrackingFrames++;
	}
	else
	{
		consecutiveBadTrackingFrames++;
		consecutiveGoodTrackingFrames = 0;
	}

	const int trackingGuardWarmupFrames = 15;
	if (scanPerformanceState.protectTrackingLoss && integrationActive &&
		processedFrameNo >= trackingGuardWarmupFrames &&
		consecutiveBadTrackingFrames >= 10)
	{
		SetIntegrationActive(false);
		lastStatusMessage = "Tracking unstable; integration paused to protect the scan.";
		needsRefresh = true;
	}
	else if (scanPerformanceState.protectTrackingLoss && !integrationActive &&
		mainLoopAction == PROCESS_VIDEO &&
		cameraReinitState == CameraReinitState::IDLE &&
		consecutiveGoodTrackingFrames >= 5)
	{
		SetIntegrationActive(true);
		lastStatusMessage = "Tracking recovered; scanning resumed.";
		needsRefresh = true;
	}

#ifndef COMPILE_WITHOUT_CUDA
	ORcudaSafeCall(cudaThreadSynchronize());
#endif
	sdkStopTimer(&timer_instant); sdkStopTimer(&timer_average);

	const float instantFrameMs = sdkGetTimerValue(&timer_instant);
	UpdateAdaptiveCpuLoad(instantFrameMs);

	processedTime = sdkGetAverageTimerValue(&timer_average);

	return true;
}

void UIEngine::Run() { glutMainLoop(); }
void UIEngine::Shutdown()
{
	if (modelSaveThread.joinable()) modelSaveThread.join();
	StopRemoteControlServer();

	ImGui_ImplOpenGL3_Shutdown();
	ImGuiGlutBridge::Shutdown();
	ImGui::DestroyContext();

	sdkDeleteTimer(&timer_instant);
	sdkDeleteTimer(&timer_average);

	if (rgbVideoWriter != NULL) delete rgbVideoWriter;
	if (depthVideoWriter != NULL) delete depthVideoWriter;

	ReleaseImages();

	if (ownsEngineObjects)
	{
		delete mainEngine;
		delete imageSource;
		if (imuSource != NULL) delete imuSource;
	}

	delete[] outFolder;
	delete instance;
	instance = NULL;
}
