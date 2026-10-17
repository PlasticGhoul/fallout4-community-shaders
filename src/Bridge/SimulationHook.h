#pragma once

#include <string>

namespace Bridge
{
	/// BSInputDeviceManager::PollInputDevices, once per game frame before its
	/// simulation: where Streamline's frame token and Reflex's sleep belong.
	/// Installed from F4SEPlugin_Load after the runtime check; it forwards to
	/// Bridge::OnSimulationStart, which does nothing while there is no bridge.
	[[nodiscard]] bool InstallSimulationHook() noexcept;
	[[nodiscard]] bool SimulationHookInstalled() noexcept;

	/// Calls per caller since the last report, for the once-a-second line;
	/// resets the counts.
	[[nodiscard]] std::string TakeSimulationReport();
}
