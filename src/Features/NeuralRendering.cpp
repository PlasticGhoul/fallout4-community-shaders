#include "Features/NeuralRendering.h"

#include "Bridge/Bridge.h"
#include "Bridge/Capture.h"
#include "Bridge/NeuralDecision.h"
#include "Menu/MenuSystem.h"
#include "Settings/Settings.h"
#include "Shader/ShaderCompiler.h"
#include "Shader/ShaderSource.h"
#include "Upscale/EngineHooks.h"
#include "Util/GamePaths.h"

#include <RE/M/Main.h>
#include <RE/U/UI.h>

#include <format>
#include <optional>
#include <vector>

namespace Features
{
	namespace
	{
		constexpr std::uint64_t kLogInterval = 180;

		/// The engine's TAA switch: without it the engine writes no motion
		/// vectors (F5.5a), and NR cannot run without them.
		constexpr std::uint64_t kEnableTAA = 2704658;

		/// NGX is started once per process and stays up; a later Setup, after
		/// the overlay switched the feature off and on, reuses the answer.
		std::optional<std::string>& StartResult() noexcept
		{
			static std::optional<std::string> result;
			return result;
		}
	}

	void NeuralRendering::Declare()
	{
		Settings::DeclareFeature("NeuralRendering", false)
			.Label("feature.neuralrendering.name", "Neural Rendering")
			.Help(
				"feature.neuralrendering.help",
				"NVIDIA DLSS Neural Rendering (DLSS 5) re-renders the finished picture with a neural network for more "
				"lifelike light, materials and contrast; the HUD stays untouched. Needs an RTX 50 GPU, the Frame Generation "
				"archive and borderless windowed mode. Switching it on takes effect at the next game start unless Frame "
				"Generation was already on. Changing the model stalls the picture once. Uses NVIDIA DLSS technology.");
		Settings::DeclareChoice("NeuralRendering/model", "fast", std::vector<std::string>{ "fast", "original" })
			.Label("feature.neuralrendering.model", "Model")
			.Help("feature.neuralrendering.model_help", "Fast is NVIDIA's newer model at about half the cost; original is the first one.");
		Settings::DeclareChoice("NeuralRendering/style", "1", std::vector<std::string>{ "1", "2", "3" })
			.Label("feature.neuralrendering.style", "Style")
			.Help("feature.neuralrendering.style_help", "Three variants of the look; which suits a scene is a matter of taste.");
		Settings::DeclareSlider("NeuralRendering/intensity", 1.0, 0.0, 1.0)
			.Label("feature.neuralrendering.intensity", "Strength")
			.Help("feature.neuralrendering.intensity_help", "Overall strength of the effect; zero leaves the picture unchanged.");
		Settings::DeclareSlider("NeuralRendering/structure", 0.7, 0.0, 1.0)
			.Label("feature.neuralrendering.structure", "Structure")
			.Help("feature.neuralrendering.structure_help", "How far details, shadows and materials are reworked.");
		Settings::DeclareSlider("NeuralRendering/tone", 0.3, 0.0, 1.0)
			.Label("feature.neuralrendering.tone", "Tone")
			.Help("feature.neuralrendering.tone_help", "How far lightness, darkness and colour are reworked.");
		Settings::DeclareBool("NeuralRendering/characters", true)
			.Label("feature.neuralrendering.characters", "Recognise characters")
			.Help("feature.neuralrendering.characters_help", "Finds people in the picture so that skin gets its own strength.");
		Settings::DeclareSlider("NeuralRendering/skin", 0.5, 0.0, 1.0)
			.Label("feature.neuralrendering.skin", "Skin")
			.Help("feature.neuralrendering.skin_help", "How far the structure of skin is reworked; needs character recognition.");
		Settings::DeclareChoice("NeuralRendering/view", "off", std::vector<std::string>{ "off", "split", "mask" })
			.Label("feature.neuralrendering.view", "Debug view")
			.Help(
				"feature.neuralrendering.view_help",
				"Split shows the picture without neural rendering on the left half. Mask tints what was left as it was: "
				"only the HUD and the overlay should be.");
	}

	bool NeuralRendering::Setup()
	{
		_frames = 0;
		_blocker = "<start>";

		if (!Bridge::Active()) {
			REX::ERROR("NeuralRendering: no bridge - {}", Bridge::Refusal());
			return false;
		}
		if (!Upscale::Hooks::Installed()) {
			REX::ERROR("NeuralRendering: the engine hooks are not installed ({})", Upscale::Hooks::FailedSite());
			return false;
		}
		auto& started = StartResult();
		if (!started) {
			constexpr const char* kFile = "NeuralRendering/Composite.hlsl";
			const auto text = Shader::LoadSource(Util::DataDirectory() / "Shaders" / "FO4", kFile);
			if (!text) {
				REX::ERROR("NeuralRendering: {}", text.error());
				return false;
			}
			const auto compiled = Shader::CompileComputeShader(text->text, kFile, "main");
			if (!compiled.Succeeded()) {
				REX::ERROR("NeuralRendering: {}", compiled.diagnostics);
				return false;
			}
			started = Bridge::StartNeuralRendering(Util::PluginDataDirectory() / "NeuralRendering", compiled.bytecode);
		}
		if (!started->empty()) {
			REX::ERROR("NeuralRendering: {}", *started);
			return false;
		}
		if (!Bridge::TheCapture().Acquire(Bridge::Consumer::kNeuralRendering)) {
			REX::ERROR("NeuralRendering: the bridge capture could not start");
			return false;
		}
		_captureAcquired = true;
		REX::INFO("NeuralRendering: running");
		return true;
	}

	void NeuralRendering::Frame()
	{
		++_frames;
		auto* const ui = RE::UI::GetSingleton();
		auto* const main = RE::Main::GetSingleton();
		const Bridge::NeuralFacts facts{
			ui != nullptr && ui->GetMenuOpen("LoadingMenu"),
			ui != nullptr && ui->GetMenuOpen("PipboyMenu"),
			main != nullptr && main->inMenuMode,
			Menu::IsOverlayOpen(),
			*reinterpret_cast<const bool*>(REL::ID(kEnableTAA).address()),
		};
		const char* const blocker = Bridge::NeuralBlocker(facts);
		const bool run = blocker == nullptr;

		// Read straight from the store every frame: there is no change
		// notification, and the overlay sets these while the picture runs.
		const auto model = Settings::GetString("NeuralRendering/model");
		const auto style = Settings::GetString("NeuralRendering/style");
		const auto view = Settings::GetString("NeuralRendering/view");
		const auto options = Bridge::MakeNeuralOptions({
			model,
			style,
			view,
			Settings::GetDouble("NeuralRendering/intensity"),
			Settings::GetDouble("NeuralRendering/structure"),
			Settings::GetDouble("NeuralRendering/tone"),
			Settings::GetDouble("NeuralRendering/skin"),
			Settings::GetBool("NeuralRendering/characters"),
		});
		Bridge::TheCapture().Want(Bridge::Consumer::kNeuralRendering, run);
		Bridge::SetNeuralRendering(options, run);

		const std::string reason = blocker != nullptr ? blocker : "";
		if (reason != _blocker) {
			REX::INFO("NeuralRendering: {}", reason.empty() ? std::string{ "rendering" } : std::format("off - {}", reason));
			_blocker = reason;
		}

		if (_frames % kLogInterval == 0) {
			const auto status = Bridge::CurrentStatus();
			REX::INFO("neural rendering: {}, {:.2f} ms GPU", status.neural ? "on" : "off", status.neuralGpuMs);
			Bridge::TheCapture().LogSecond();
		}
	}

	void NeuralRendering::Shutdown()
	{
		Bridge::StopNeuralRendering();
		if (_captureAcquired) {
			Bridge::TheCapture().Release(Bridge::Consumer::kNeuralRendering);
			_captureAcquired = false;
		}
	}
}
