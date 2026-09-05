#include "Shader/ShaderCatalog.h"

#include "Shader/BSShaderLayout.h"

#include <RE/IDs_VTABLE.h>

#include <array>
#include <format>
#include <string>
#include <vector>

namespace Shader
{
	namespace
	{
		// BSShader::GetTechniqueName, vtable slot 09 by commonlibf4's numbering
		// in RE/B/BSShader.h. On x64 a member function takes its object in RCX,
		// so a free function pointer with the object as the first parameter is
		// the right shape.
		constexpr std::size_t kGetTechniqueNameSlot = 9;
		using GetTechniqueNameFn = void (*)(const void*, std::uint32_t, char*, std::uint32_t);

		// Long enough for anything the engine is likely to spell out, short
		// enough to sit on the stack of a render thread call.
		constexpr std::size_t kNameBufferSize = 256;

		// A map larger than this is reported by its count alone. Nothing is
		// gained by writing a thousand ids into a log, and the point of the
		// census is the count.
		constexpr std::size_t kMaxListed = 512;

		// Roughly a terminal width, so a long list wraps rather than producing
		// one unreadable line.
		constexpr std::size_t kLineWidth = 140;

		std::string TechniqueName(const void* a_shader, std::uint32_t a_id) noexcept
		{
			auto** const vtable = *reinterpret_cast<void** const*>(a_shader);
			if (vtable == nullptr) {
				return {};
			}

			const auto call = reinterpret_cast<GetTechniqueNameFn>(vtable[kGetTechniqueNameSlot]);

			std::array<char, kNameBufferSize> buffer{};
			call(a_shader, a_id, buffer.data(), static_cast<std::uint32_t>(buffer.size() - 1));

			// The engine may leave the buffer untouched for an id it does not
			// recognise, and a name full of control characters would mean the
			// slot is not the one we think it is. Either way, say nothing
			// rather than write rubbish into the log.
			std::string name{ buffer.data() };
			for (const auto character : name) {
				if (static_cast<unsigned char>(character) < 0x20) {
					return {};
				}
			}

			return name;
		}

		void LogStage(
			const void* a_shader,
			Stage a_stage,
			const std::vector<TechniqueEntry>& a_techniques,
			bool a_withNames) noexcept
		{
			if (a_techniques.empty()) {
				return;  // A stage this shader does not use. The normal case.
			}

			REX::INFO("    {:<8} {} technique(s)", StageName(a_stage), a_techniques.size());

			if (a_techniques.size() > kMaxListed) {
				REX::INFO("             too many to list");
				return;
			}

			std::string line;
			for (const auto& technique : a_techniques) {
				auto item = std::format("{}", technique.id);
				if (a_withNames) {
					if (const auto name = TechniqueName(a_shader, technique.id); !name.empty()) {
						item += std::format("={}", name);
					}
				}

				if (!line.empty() && line.size() + item.size() + 2 > kLineWidth) {
					REX::INFO("             {}", line);
					line.clear();
				}

				line += line.empty() ? item : ", " + item;
			}

			if (!line.empty()) {
				REX::INFO("             {}", line);
			}
		}
	}

	std::span<const ShaderClass> ShaderClasses() noexcept
	{
		// Resolved once, on the first call, which is after kGameDataReady.
		static const std::array<ShaderClass, 13> classes{
			ShaderClass{ "BSEffectShader"sv, "kEffect"sv, 0x0, RE::VTABLE::BSEffectShader[0].address() },
			ShaderClass{ "BSUtilityShader"sv, "kUtility"sv, 0x1, RE::VTABLE::BSUtilityShader[0].address() },
			ShaderClass{ "BSDistantTreeShader"sv, "kDistantTree"sv, 0x2, RE::VTABLE::BSDistantTreeShader[0].address() },
			ShaderClass{ "BSParticleShader"sv, "kParticle"sv, 0x3, RE::VTABLE::BSParticleShader[0].address() },
			ShaderClass{ "BSDFPrePassShader"sv, "kDFPrepass"sv, 0x4, RE::VTABLE::BSDFPrePassShader[0].address() },
			ShaderClass{ "BSDFLightShader"sv, "kDFLight"sv, 0x5, RE::VTABLE::BSDFLightShader[0].address() },
			ShaderClass{ "BSDFCompositeShader"sv, "kDFComposite"sv, 0x6, RE::VTABLE::BSDFCompositeShader[0].address() },
			ShaderClass{ "BSSkyShader"sv, "kSky"sv, 0x7, RE::VTABLE::BSSkyShader[0].address() },
			ShaderClass{ "BSLightingShader"sv, "kLighting"sv, 0x8, RE::VTABLE::BSLightingShader[0].address() },
			ShaderClass{ "BSBloodSplatterShader"sv, "kBloodSpatter"sv, 0x9, RE::VTABLE::BSBloodSplatterShader[0].address() },
			ShaderClass{ "BSWaterShader"sv, "kWater"sv, 0xA, RE::VTABLE::BSWaterShader[0].address() },
			ShaderClass{ "BSFaceCustomizationShader"sv, "kFaceCustomization"sv, 0xB, RE::VTABLE::BSFaceCustomizationShader[0].address() },
			ShaderClass{ "BSImagespaceShader"sv, "kImageSpace"sv, 0xC, RE::VTABLE::BSImagespaceShader[0].address() },
		};

		return classes;
	}

	void ReportShaderTechniques(
		const ShaderClass& a_class,
		const void* a_shader,
		bool a_withNames) noexcept
	{
		if (a_shader == nullptr) {
			REX::INFO("{:<26} {:<18} never seen", a_class.className, a_class.enumerator);
			return;
		}

		const auto type = ShaderType(a_shader);
		const auto* const fxp = FxpFilename(a_shader);

		REX::INFO(
			"{:<26} {:<18} shaderType {} ({}), fxp {}",
			a_class.className,
			a_class.enumerator,
			type,
			type == a_class.shaderType ? "as expected" : "MISMATCH",
			fxp != nullptr ? fxp : "(none)");

		for (auto stage = 0; stage < static_cast<int>(Stage::kTotal); ++stage) {
			const auto which = static_cast<Stage>(stage);
			LogStage(a_shader, which, Techniques(a_shader, which), a_withNames);
		}
	}
}
