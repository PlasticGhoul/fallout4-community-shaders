#include "Feature/FeatureSystem.h"
#include "Plugin.h"
#include "Render/SwapChainHook.h"
#include "Runtime.h"

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
			Render::InstallSwapChainHook();
			Features::StartSystem();
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
	initInfo.trampoline = false;  // subproject A installs no hooks
	initInfo.hook = false;        // no REL::FHook objects are registered either

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

	REX::INFO("loaded");
	return true;
}
