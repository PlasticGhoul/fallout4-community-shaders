#include "Bridge/Bridge.h"
#include "Bridge/SimulationHook.h"
#include "Bridge/StartupSwitch.h"
#include "Feature/FeatureSystem.h"
#include "Menu/MenuSystem.h"
#include "Plugin.h"
#include "Render/FramePhase.h"
#include "Render/SwapChainHook.h"
#include "Render/TechniqueTracker.h"
#include "Runtime.h"
#include "Settings/Settings.h"
#include "Upscale/EngineHooks.h"
#include "Util/GamePaths.h"

namespace
{
	void MessageHandler(F4SE::MessagingInterface::Message* a_message)
	{
		if (a_message == nullptr) {
			return;
		}

		switch (a_message->type) {
		case F4SE::MessagingInterface::kPostPostLoad:
			REX::INFO("kPostPostLoad received");
			break;
		case F4SE::MessagingInterface::kGameDataReady:
			REX::INFO("kGameDataReady received");
			// Features first. Installing the hook makes Present live on the
			// render thread, and Present drives the registry - registering into
			// a vector that another thread may already be walking is a race we
			// can avoid entirely by ordering, without a flag to synchronise on.
			// Before the features, because their StartSystem is what loads the
			// settings file, and a setting has to be declared before that.
			Menu::StartSystem();
			Features::StartSystem();

			// Before the Present hook, and therefore before any feature's Setup
			// can run: ShaderCensus patches the same vtable slot while it
			// counts and restores it when it is done. Underneath it our thunk
			// survives that restore; on top of it, it would be overwritten and
			// the phase would go silent without a word.
			static_cast<void>(Render::InstallFramePhase());

			// Same slots, same rule: before the Present hook, so that a census
			// restores our thunk and not the engine's entry over it.
			static_cast<void>(Render::InstallTechniqueTracker());

			Render::InstallSwapChainHook();
			break;
		default:
			break;
		}
	}
}

extern "C" [[maybe_unused]] __declspec(dllexport) constinit auto F4SEPlugin_Version = []() noexcept {
	F4SE::PluginVersionData data{};

	data.PluginName(Plugin::NAME.data());
	data.PluginVersion(Plugin::VERSION);
	data.AuthorName("PlasticGhoul");
	data.UsesAddressLibrary(true);
	data.UsesSigScanning(false);
	data.IsLayoutDependent(true);
	data.HasNoStructUse(false);
	// Pinned rather than RUNTIME_LATEST: a moving target would silently admit
	// an untested game version after the next Bethesda patch.
	data.CompatibleVersions({ F4SE::RUNTIME_1_11_240 });

	return data;
}();

extern "C" __declspec(dllexport) bool F4SEAPI F4SEPlugin_Query(
	const F4SE::QueryInterface*,
	F4SE::PluginInfo* a_info)
{
	a_info->infoVersion = F4SE::PluginInfo::kVersion;
	a_info->name = Plugin::NAME.data();
	a_info->version = Plugin::VERSION[0];
	return true;
}

extern "C" __declspec(dllexport) bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface* a_f4se)
{
	F4SE::InitInfo initInfo{};
	// Eight call hooks of F5.5a, fourteen bytes of branch each; the rest is
	// headroom. The two prolog hooks go through Detours, not the trampoline.
	// hook stays false: our hooks are made after the runtime check, not
	// registered statically, because a REL::THook reads its site the moment
	// it is constructed.
	initInfo.trampoline = true;
	initInfo.trampolineSize = 256;
	initInfo.hook = false;

	// Keeps the last five runs instead of truncating on every start. Each game
	// start costs real time to produce, and diagnosing one run while a second
	// one overwrites its evidence is how a finding gets lost. F4SE turns this
	// into a rotating sink that rotates once per process start.
	initInfo.logRotate = 5;

	// Must run before the version check: Init is what opens the log channel,
	// and a refusal that cannot be logged is a refusal nobody can diagnose.
	F4SE::Init(a_f4se, initInfo);

	const auto runtime = a_f4se->RuntimeVersion();
	REX::INFO("build {}", Plugin::BUILD_DESCRIBE);
	REX::INFO("game runtime {}, resolved bucket {}", runtime, Runtime::BucketName());

	if (!Runtime::IsSupported(runtime)) {
		REX::ERROR(
			"unsupported game version {}; this build is validated against {} only, refusing to load",
			runtime,
			Runtime::kSupported);
		return false;
	}

	const auto* messaging = F4SE::GetMessagingInterface();
	if (messaging == nullptr || !messaging->RegisterListener(MessageHandler)) {
		REX::ERROR("failed to register the F4SE message listener");
		return false;
	}

	// Installed at load, not when the DLSS feature starts: a call hook written
	// while the render thread runs the code under it is a race. Without a
	// handler every hook passes straight through. A failure costs the DLSS
	// feature, not the plugin.
	if (!Upscale::Hooks::Install()) {
		REX::ERROR("upscale hooks not installed ({}); DLSS will refuse", Upscale::Hooks::FailedSite());
	}

	// Long before the game creates its device, which is where the bridge is
	// decided - from the switch as the file holds it now, since Settings::Init
	// comes only with kGameDataReady.
	const auto startupSwitch = Bridge::ReadStartupSwitch(Settings::DefaultFile());
	static_cast<void>(Bridge::InstallDeviceHook({ startupSwitch.on, startupSwitch.reason, Util::PluginDataDirectory() }));
	// Only with the switch on: without a bridge the detour would do nothing
	// but sit in front of anyone else's hook on that function.
	if (startupSwitch.on && !Bridge::InstallSimulationHook()) {
		REX::ERROR("frame generation will refuse: no frame start for Reflex");
	}

	REX::INFO("loaded");
	return true;
}
