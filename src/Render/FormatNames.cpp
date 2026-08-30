#include "Render/FormatNames.h"

#include <utility>

namespace Render
{
	std::string_view FormatName(REX::W32::DXGI_FORMAT a_format) noexcept
	{
		using namespace REX::W32;

		switch (a_format) {
		case DXGI_FORMAT_UNKNOWN:
			return "UNKNOWN"sv;

		case DXGI_FORMAT_R32G32B32A32_TYPELESS:
			return "R32G32B32A32_TYPELESS"sv;
		case DXGI_FORMAT_R32G32B32A32_FLOAT:
			return "R32G32B32A32_FLOAT"sv;
		case DXGI_FORMAT_R32G32B32A32_UINT:
			return "R32G32B32A32_UINT"sv;
		case DXGI_FORMAT_R32G32B32_FLOAT:
			return "R32G32B32_FLOAT"sv;

		case DXGI_FORMAT_R16G16B16A16_TYPELESS:
			return "R16G16B16A16_TYPELESS"sv;
		case DXGI_FORMAT_R16G16B16A16_FLOAT:
			return "R16G16B16A16_FLOAT"sv;
		case DXGI_FORMAT_R16G16B16A16_UNORM:
			return "R16G16B16A16_UNORM"sv;
		case DXGI_FORMAT_R16G16B16A16_SNORM:
			return "R16G16B16A16_SNORM"sv;

		case DXGI_FORMAT_R32G32_FLOAT:
			return "R32G32_FLOAT"sv;
		case DXGI_FORMAT_R10G10B10A2_UNORM:
			return "R10G10B10A2_UNORM"sv;
		case DXGI_FORMAT_R11G11B10_FLOAT:
			return "R11G11B10_FLOAT"sv;

		case DXGI_FORMAT_R8G8B8A8_TYPELESS:
			return "R8G8B8A8_TYPELESS"sv;
		case DXGI_FORMAT_R8G8B8A8_UNORM:
			return "R8G8B8A8_UNORM"sv;
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
			return "R8G8B8A8_UNORM_SRGB"sv;
		case DXGI_FORMAT_R8G8B8A8_SNORM:
			return "R8G8B8A8_SNORM"sv;

		case DXGI_FORMAT_R16G16_FLOAT:
			return "R16G16_FLOAT"sv;
		case DXGI_FORMAT_R16G16_UNORM:
			return "R16G16_UNORM"sv;
		case DXGI_FORMAT_R16G16_SNORM:
			return "R16G16_SNORM"sv;

		case DXGI_FORMAT_R32_TYPELESS:
			return "R32_TYPELESS"sv;
		case DXGI_FORMAT_D32_FLOAT:
			return "D32_FLOAT"sv;
		case DXGI_FORMAT_R32_FLOAT:
			return "R32_FLOAT"sv;
		case DXGI_FORMAT_R32_UINT:
			return "R32_UINT"sv;
		case DXGI_FORMAT_R32_SINT:
			return "R32_SINT"sv;

		case DXGI_FORMAT_R24G8_TYPELESS:
			return "R24G8_TYPELESS"sv;
		case DXGI_FORMAT_D24_UNORM_S8_UINT:
			return "D24_UNORM_S8_UINT"sv;
		case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
			return "R24_UNORM_X8_TYPELESS"sv;

		case DXGI_FORMAT_R8G8_UNORM:
			return "R8G8_UNORM"sv;
		case DXGI_FORMAT_R8G8_SNORM:
			return "R8G8_SNORM"sv;

		case DXGI_FORMAT_R16_TYPELESS:
			return "R16_TYPELESS"sv;
		case DXGI_FORMAT_R16_FLOAT:
			return "R16_FLOAT"sv;
		case DXGI_FORMAT_D16_UNORM:
			return "D16_UNORM"sv;
		case DXGI_FORMAT_R16_UNORM:
			return "R16_UNORM"sv;
		case DXGI_FORMAT_R16_UINT:
			return "R16_UINT"sv;

		case DXGI_FORMAT_R8_UNORM:
			return "R8_UNORM"sv;
		case DXGI_FORMAT_R8_UINT:
			return "R8_UINT"sv;
		case DXGI_FORMAT_A8_UNORM:
			return "A8_UNORM"sv;
		case DXGI_FORMAT_B8G8R8A8_UNORM:
			return "B8G8R8A8_UNORM"sv;

		default:
			return "UNKNOWN"sv;
		}
	}

	std::string BindFlagsString(std::uint32_t a_bindFlags)
	{
		using namespace REX::W32;

		// Fixed order, so identical flag sets always render identically and the
		// log stays comparable between runs.
		static constexpr std::pair<std::uint32_t, std::string_view> kFlags[]{
			{ D3D11_BIND_RENDER_TARGET, "RENDER_TARGET"sv },
			{ D3D11_BIND_SHADER_RESOURCE, "SHADER_RESOURCE"sv },
			{ D3D11_BIND_UNORDERED_ACCESS, "UNORDERED_ACCESS"sv },
			{ D3D11_BIND_DEPTH_STENCIL, "DEPTH_STENCIL"sv },
			{ D3D11_BIND_VERTEX_BUFFER, "VERTEX_BUFFER"sv },
			{ D3D11_BIND_INDEX_BUFFER, "INDEX_BUFFER"sv },
			{ D3D11_BIND_CONSTANT_BUFFER, "CONSTANT_BUFFER"sv },
			{ D3D11_BIND_STREAM_OUTPUT, "STREAM_OUTPUT"sv },
			{ D3D11_BIND_DECODER, "DECODER"sv },
			{ D3D11_BIND_VIDEO_ENCODER, "VIDEO_ENCODER"sv },
		};

		std::string result;
		for (const auto& [bit, name] : kFlags) {
			if ((a_bindFlags & bit) != 0) {
				if (!result.empty()) {
					result += '|';
				}
				result.append(name);
			}
		}

		return result.empty() ? std::string{ "-" } : result;
	}
}
