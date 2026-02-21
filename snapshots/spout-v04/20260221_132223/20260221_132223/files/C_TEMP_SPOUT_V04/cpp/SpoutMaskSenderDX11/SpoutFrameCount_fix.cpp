// Fix compilation of SpoutFrameCount.cpp by ensuring multimedia time declarations are available.
// We do NOT modify third_party. We compile this TU instead of SpoutFrameCount.cpp.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")

#include "../../third_party/SpoutSDK/SpoutGL/SpoutFrameCount.cpp"