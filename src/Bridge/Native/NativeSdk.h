#pragma once

// Only for src/Bridge/Native, which is compiled without the precompiled
// header (CMakeLists.txt): D3D12 and Streamline need the Windows SDK's own
// headers, and nothing else in the plugin may see them. windef.h defines near
// and far as empty macros - neither is a usable name in this directory.
#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>

#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

// Streamline's headers paste tokens in SL_FEATURE_FUN_IMPORT, which
// /Zc:preprocessor reports as C5103 (Skyrim CS built them with /wd5103), and
// declare what they never use. Their warnings are theirs.
#pragma warning(push, 0)
#include <sl.h>
#include <sl_dlss_g.h>
#include <sl_pcl.h>
#include <sl_reflex.h>
#pragma warning(pop)

namespace Bridge::Native
{
	using Microsoft::WRL::ComPtr;
}
