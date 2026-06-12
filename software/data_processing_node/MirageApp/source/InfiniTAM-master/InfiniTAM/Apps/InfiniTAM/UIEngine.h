// Copyright 2014-2017 Oxford University Innovation Limited and the authors of InfiniTAM

#pragma once

#include "../../InputSource/ImageSourceEngine.h"
#include "../../InputSource/IMUSourceEngine.h"
#include "../../InputSource/FFMPEGWriter.h"
#include "../../ITMLib/Core/ITMMainEngine.h"
#include "../../ITMLib/Utils/ITMLibSettings.h"
#include "../../ORUtils/FileUtils.h"
#include "../../ORUtils/NVTimer.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace InfiniTAM
{
	namespace Engine
	{
		class UIEngine
		{
			static UIEngine* instance;

			enum MainLoopAction
			{
				PROCESS_PAUSED, PROCESS_VIDEO, EXIT, SAVE_TO_DISK
			}mainLoopAction;

			enum ScanControlCommand
			{
				SCAN_CONTROL_START,
				SCAN_CONTROL_PAUSE,
				SCAN_CONTROL_STOP,
				SCAN_CONTROL_RESET
			};

			struct UIColourMode {
				const char *name;
				ITMLib::ITMMainEngine::GetImageType type;
				UIColourMode(const char *_name, ITMLib::ITMMainEngine::GetImageType _type)
				 : name(_name), type(_type)
				{}
			};
			std::vector<UIColourMode> colourModes_main, colourModes_freeview;
			int currentColourMode;

			InputSource::ImageSourceEngine *imageSource;
			InputSource::IMUSourceEngine *imuSource;
			ITMLib::ITMLibSettings internalSettings;
			const ITMLib::ITMLibSettings *engineSettings;
			ITMLib::ITMMainEngine *mainEngine;
			bool ownsEngineObjects;
			std::string calibFilename;
			std::string lastStatusMessage;
			std::thread remoteControlThread;
			std::mutex remoteControlMutex;
			std::vector<ScanControlCommand> pendingRemoteControlCommands;
			std::atomic<bool> remoteControlRunning;
			int remoteControlPort;

			StopWatchInterface *timer_instant;
			StopWatchInterface *timer_average;

		private:
			static const int NUM_WIN = 3;
			Vector4f winReg[NUM_WIN];
			Vector2i winSize;
			uint textureId[NUM_WIN];
			ITMUChar4Image *outImage[NUM_WIN];
			ITMLib::ITMMainEngine::GetImageType outImageType[NUM_WIN];

			ITMUChar4Image *inputRGBImage; ITMShortImage *inputRawDepthImage;
			ITMLib::ITMIMUMeasurement *inputIMUMeasurement;

			bool freeviewActive;
			bool integrationActive;
			ORUtils::SE3Pose freeviewPose;
			ITMLib::ITMIntrinsics freeviewIntrinsics;

			int mouseState;
			Vector2i mouseLastClick;
			bool mouseWarped;
			bool imguiKeyboardCaptured;

			int currentFrameNo; bool isRecording;
			int adaptiveFrameStride;
			int adaptiveSlowFrameCount;
			int adaptiveFastFrameCount;
			float adaptiveLastFrameMs;
			InputSource::FFMPEGWriter *rgbVideoWriter;
			InputSource::FFMPEGWriter *depthVideoWriter;

			struct D415GuiState
			{
				int connectionModeIndex;
				char networkAddress[64];
				int resolutionIndex;
				int presetIndex;
				int laserPower;
				bool cameraInitialised;
				bool applyingCamera;
			} d415GuiState;

			struct ScanPerformanceState
			{
				int frameStrideIndex;
				int scanAreaIndex;
				int meshDetailIndex;
				int maxDepthCm;
				int guideDepthCm;
				bool restrictScanArea;
				bool showScanAreaOverlay;
				bool useStableCpuResolution;
				bool useAdaptiveCpuLoad;
				bool approximateRaycast;
				bool protectTrackingLoss;
				bool useColourTracking;
			} scanPerformanceState;

			void AllocateImages(ITMLib::ITMLibSettings::DeviceType deviceType);
			void ReleaseImages();
			ITMLib::ITMMainEngine *CreateMainEngineForImageSource(InputSource::ImageSourceEngine *source) const;
			void ReplaceImageSource(InputSource::ImageSourceEngine *newImageSource, InputSource::IMUSourceEngine *newIMUSource);
			void ApplyLivePerformanceSettings();
			void ResetAdaptiveCpuLoad();
			void UpdateAdaptiveCpuLoad(float frameMs);
			int GetFrameStride() const;
			float GetScanAreaSizeM() const;
			float GetDepthScaleM() const;
			void ApplyScanAreaMask(ITMShortImage *rawDepthImage);
			void DrawScanAreaOverlay();
			void DrawScanAreaOverlayForWindow(int windowIndex, const ITMLib::ITMIntrinsics& intrinsics) const;
			void ApplyEngineIntegrationState(bool active);
			void SetIntegrationActive(bool active);
			void ApplyScanControlCommand(ScanControlCommand command, bool remoteCommand);
			void ToggleFreeview();
			void ResetScene();
			void SaveSceneState();
			void SaveModel();
			void ApplyAndInitialiseD415();
			void StartRemoteControlServer();
			void StopRemoteControlServer();
			void RemoteControlThreadMain();
			void QueueRemoteControlCommand(ScanControlCommand command);
			void ProcessRemoteControlCommands();
			void RenderControlPanel();

			bool IsControlPanelHit(int x, int y) const;
			bool WantsMouseCapture() const;
			bool WantsKeyboardCapture() const;
		public:
			static UIEngine* Instance(void) {
				if (instance == NULL) instance = new UIEngine();
				return instance;
			}

			static void glutDisplayFunction();
			static void glutIdleFunction();
			static void glutReshapeFunction(int width, int height);
			static void glutKeyDownFunction(unsigned char key, int x, int y);
			static void glutKeyUpFunction(unsigned char key, int x, int y);
			static void glutMouseButtonFunction(int button, int state, int x, int y);
			static void glutMouseMoveFunction(int x, int y);
			static void glutMouseWheelFunction(int button, int dir, int x, int y);

			const Vector2i & getWindowSize(void) const
			{ return winSize; }

			float processedTime;
			int processedFrameNo;
			int trackingResult;
			char *outFolder;
			bool needsRefresh;
			ITMUChar4Image *saveImage;

			void Initialise(int & argc, char** argv, InputSource::ImageSourceEngine *imageSource, InputSource::IMUSourceEngine *imuSource,
				ITMLib::ITMMainEngine *mainEngine, const char *outFolder, const ITMLib::ITMLibSettings *internalSettings,
				const char *calibFilename);
			void Shutdown();

			void Run();
			bool ProcessFrame();
			
			void GetScreenshot(ITMUChar4Image *dest) const;
			void SaveScreenshot(const char *filename) const;
		};
	}
}
