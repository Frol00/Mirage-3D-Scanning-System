// Minimal Dear ImGui input bridge for the GLUT-based InfiniTAM viewer.

#include "ImGuiGlutBridge.h"

#include <imgui.h>

#include <cstring>

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#endif

using namespace InfiniTAM::Engine;

namespace
{
	double g_Time = 0.0;

	ImGuiKey ToImGuiKey(unsigned char key)
	{
		if ((key >= 'a') && (key <= 'z')) return static_cast<ImGuiKey>(ImGuiKey_A + (key - 'a'));
		if ((key >= 'A') && (key <= 'Z')) return static_cast<ImGuiKey>(ImGuiKey_A + (key - 'A'));
		if ((key >= '0') && (key <= '9')) return static_cast<ImGuiKey>(ImGuiKey_0 + (key - '0'));

		switch (key)
		{
		case 8: return ImGuiKey_Backspace;
		case 9: return ImGuiKey_Tab;
		case 13: return ImGuiKey_Enter;
		case 27: return ImGuiKey_Escape;
		case ' ': return ImGuiKey_Space;
		case '\'': return ImGuiKey_Apostrophe;
		case ',': return ImGuiKey_Comma;
		case '-': return ImGuiKey_Minus;
		case '.': return ImGuiKey_Period;
		case '/': return ImGuiKey_Slash;
		case ';': return ImGuiKey_Semicolon;
		case '=': return ImGuiKey_Equal;
		case '[': return ImGuiKey_LeftBracket;
		case '\\': return ImGuiKey_Backslash;
		case ']': return ImGuiKey_RightBracket;
		case '`': return ImGuiKey_GraveAccent;
		default: return ImGuiKey_None;
		}
	}

	void UpdateModifierKeys()
	{
		ImGuiIO& io = ImGui::GetIO();
		const int mods = glutGetModifiers();
		io.AddKeyEvent(ImGuiMod_Ctrl, (mods & GLUT_ACTIVE_CTRL) != 0);
		io.AddKeyEvent(ImGuiMod_Shift, (mods & GLUT_ACTIVE_SHIFT) != 0);
		io.AddKeyEvent(ImGuiMod_Alt, (mods & GLUT_ACTIVE_ALT) != 0);
	}
}

void ImGuiGlutBridge::Init()
{
	g_Time = 0.0;

	ImGuiIO& io = ImGui::GetIO();
	io.BackendPlatformName = "imgui_impl_glut_minimal";
	io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;

	std::memset(io.MouseDown, 0, sizeof(io.MouseDown));
}

void ImGuiGlutBridge::Shutdown()
{
	ImGuiIO& io = ImGui::GetIO();
	io.BackendPlatformName = NULL;
}

void ImGuiGlutBridge::NewFrame()
{
	ImGuiIO& io = ImGui::GetIO();

	const int width = glutGet(GLUT_WINDOW_WIDTH);
	const int height = glutGet(GLUT_WINDOW_HEIGHT);
	io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));

	const int elapsedMs = glutGet(GLUT_ELAPSED_TIME);
	const double currentTime = elapsedMs / 1000.0;
	io.DeltaTime = g_Time > 0.0 ? static_cast<float>(currentTime - g_Time) : (1.0f / 60.0f);
	g_Time = currentTime;
}

void ImGuiGlutBridge::KeyboardDown(unsigned char key, int x, int y)
{
	ImGuiIO& io = ImGui::GetIO();
	io.AddMousePosEvent(static_cast<float>(x), static_cast<float>(y));
	UpdateModifierKeys();

	const ImGuiKey imguiKey = ToImGuiKey(key);
	if (imguiKey != ImGuiKey_None) io.AddKeyEvent(imguiKey, true);

	if ((key >= 32) && (key < 127)) io.AddInputCharacter(key);
}

void ImGuiGlutBridge::KeyboardUp(unsigned char key, int x, int y)
{
	ImGuiIO& io = ImGui::GetIO();
	io.AddMousePosEvent(static_cast<float>(x), static_cast<float>(y));
	UpdateModifierKeys();

	const ImGuiKey imguiKey = ToImGuiKey(key);
	if (imguiKey != ImGuiKey_None) io.AddKeyEvent(imguiKey, false);
}

void ImGuiGlutBridge::MouseButton(int button, int state, int x, int y)
{
	ImGuiIO& io = ImGui::GetIO();
	io.AddMousePosEvent(static_cast<float>(x), static_cast<float>(y));
	UpdateModifierKeys();

	int imguiButton = -1;
	switch (button)
	{
	case GLUT_LEFT_BUTTON: imguiButton = 0; break;
	case GLUT_RIGHT_BUTTON: imguiButton = 1; break;
	case GLUT_MIDDLE_BUTTON: imguiButton = 2; break;
	default: break;
	}

	if (imguiButton >= 0) io.AddMouseButtonEvent(imguiButton, state == GLUT_DOWN);
}

void ImGuiGlutBridge::MouseMove(int x, int y)
{
	ImGui::GetIO().AddMousePosEvent(static_cast<float>(x), static_cast<float>(y));
}

void ImGuiGlutBridge::MouseWheel(int dir, int x, int y)
{
	ImGuiIO& io = ImGui::GetIO();
	io.AddMousePosEvent(static_cast<float>(x), static_cast<float>(y));
	io.AddMouseWheelEvent(0.0f, dir > 0 ? 1.0f : -1.0f);
}
