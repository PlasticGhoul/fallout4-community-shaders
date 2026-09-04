#pragma once

#include "Feature/Feature.h"
#include "Shader/ShaderOverride.h"
#include "Util/FileWatch.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace Features
{
	/// Subproject C's shader pipeline, as a feature.
	///
	/// It is here rather than in a dummy because it owns real state: a created
	/// ID3D11PixelShader and a pointer written into engine memory. A feature
	/// that owns nothing cannot show whether Shutdown is honest.
	class ImagespaceTint : public Feature
	{
	public:
		/// Stops the watcher. Never runs in the game - the registry that owns
		/// the features is leaked on purpose - but a stack instance must not be
		/// able to reach ~thread with the thread still running, which would be
		/// a straight std::terminate.
		~ImagespaceTint() override;

		[[nodiscard]] std::string_view Name() const override { return "ImagespaceTint"; }
		void Declare() override;
		[[nodiscard]] bool Setup() override;
		void Frame() override;
		void Shutdown() override;

	private:
		void WatcherLoop();
		void CompileAndPublish();
		void TryCatalog();

		Shader::PixelShaderOverride _override;
		Util::FileWatch _watch;

		std::thread _watcher;
		std::mutex _mutex;
		std::vector<std::uint8_t> _pending;
		std::atomic<bool> _hasPending{ false };
		std::atomic<bool> _stop{ false };

		std::uint64_t _frames{ 0 };
		std::uint64_t _catalogTries{ 0 };
		bool _catalogDone{ false };

		// Not reset between enables: the pass table is worth one look per
		// session, not one per toggle.
		bool _tableLogged{ false };
	};
}
