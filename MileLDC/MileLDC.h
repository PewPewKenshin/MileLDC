#pragma once

#include <Windows.h>

#include "_ImGui\\imgui.h"
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "_ImGui\\imgui_internal.h"

DWORD __stdcall Entry(LPVOID module);