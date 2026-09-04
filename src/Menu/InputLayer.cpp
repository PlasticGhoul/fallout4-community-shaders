#include "Menu/InputLayer.h"

#include <RE/B/BSInputEnableLayer.h>
#include <RE/B/BSInputEnableManager.h>

namespace Menu
{
	bool InputLayer::Suppress() noexcept
	{
		if (_layer != nullptr) {
			return true;
		}

		auto* const manager = RE::BSInputEnableManager::GetSingleton();
		if (manager == nullptr) {
			REX::ERROR("no input enable manager, the overlay opens without one");
			return false;
		}

		RE::BSTSmartPointer<RE::BSInputEnableLayer> layer;
		if (!manager->AllocateNewLayer(layer, "CommunityShadersFO4") || !layer) {
			REX::ERROR("could not allocate an input layer, the overlay opens without one");
			return false;
		}

		// kAll is -1: movement, looking, fighting, VATS and the rest, all at
		// once. A menu that leaves any of them on is a menu you fight with.
		manager->EnableUserEvent(
			layer->layerID,
			RE::UserEvents::USER_EVENT_FLAG::kAll,
			false,
			RE::UserEvents::SENDER_ID::kMenu);

		// The smart pointer releases its own reference at the end of this
		// scope, and the layer has to outlive it. The manager keeps one of its
		// own in layerWrappers, so the count never reaches zero here.
		layer->IncRef();
		_layer = layer.get();

		REX::INFO("input layer {} acquired", layer->layerID);
		return true;
	}

	void InputLayer::Restore() noexcept
	{
		if (_layer == nullptr) {
			return;
		}

		auto* const layer = static_cast<RE::BSInputEnableLayer*>(_layer);
		auto* const manager = RE::BSInputEnableManager::GetSingleton();

		if (manager != nullptr) {
			manager->EnableUserEvent(
				layer->layerID,
				RE::UserEvents::USER_EVENT_FLAG::kAll,
				true,
				RE::UserEvents::SENDER_ID::kMenu);
		}

		static_cast<void>(layer->DecRef());
		_layer = nullptr;

		REX::INFO("input layer released");
	}
}
