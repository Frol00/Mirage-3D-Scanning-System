// Minimal Dear ImGui input bridge for the GLUT-based InfiniTAM viewer.

#pragma once

namespace InfiniTAM
{
	namespace Engine
	{
		namespace ImGuiGlutBridge
		{
			void Init();
			void Shutdown();
			void NewFrame();
			void KeyboardDown(unsigned char key, int x, int y);
			void KeyboardUp(unsigned char key, int x, int y);
			void MouseButton(int button, int state, int x, int y);
			void MouseMove(int x, int y);
			void MouseWheel(int dir, int x, int y);
		}
	}
}
