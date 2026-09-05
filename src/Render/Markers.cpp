#include "Render/Markers.h"

#include "Render/Renderer.h"

#include <REX/W32/D3D11_1.h>

namespace Render
{
	namespace
	{
		REX::W32::ID3DUserDefinedAnnotation* g_annotation = nullptr;
	}

	bool InitMarkers() noexcept
	{
		if (g_annotation != nullptr) {
			return true;
		}

		auto* const context = GetContext();
		if (context == nullptr) {
			return false;
		}

		REX::W32::ID3DUserDefinedAnnotation* annotation = nullptr;
		const auto result = context->QueryInterface(
			REX::W32::IID_ID3DUserDefinedAnnotation,
			reinterpret_cast<void**>(std::addressof(annotation)));

		if (result < 0 || annotation == nullptr) {
			REX::INFO("ID3DUserDefinedAnnotation unavailable, markers disabled");
			return false;
		}

		// Held for the life of the process, like the context it came from.
		g_annotation = annotation;
		REX::INFO("debug markers available");
		return true;
	}

	bool PushMarker(const wchar_t* a_name) noexcept
	{
		if (g_annotation == nullptr || a_name == nullptr) {
			return false;
		}

		g_annotation->BeginEvent(a_name);
		return true;
	}

	void PopMarker() noexcept
	{
		if (g_annotation != nullptr) {
			g_annotation->EndEvent();
		}
	}

	MarkerScope::MarkerScope(const wchar_t* a_name) noexcept :
		m_open(PushMarker(a_name))
	{}

	MarkerScope::~MarkerScope() noexcept
	{
		if (m_open) {
			PopMarker();
		}
	}
}
