#pragma once

#include <string_view>

namespace Features
{
	class Feature
	{
	public:
		virtual ~Feature() = default;

		/// Stable key, and the settings path prefix. Changing it after a user
		/// has a settings file silently resets their choice, so it must not
		/// change.
		[[nodiscard]] virtual std::string_view Name() const = 0;

		/// Declares the feature's settings, including its own enable switch.
		/// Called by the registry when the feature is registered, which is
		/// before Settings::Init - a REX setting registers with its store at
		/// construction, and Init is what walks that registration, so a
		/// declaration arriving after it would never be read from the file.
		///
		/// Declaring is not setting up: this runs whether or not the feature
		/// will ever be enabled, and must touch nothing but Settings.
		virtual void Declare() {}

		/// Acquires what the feature needs. Returning false refuses the enable:
		/// the registry logs it once and does not try again until the settings
		/// change.
		///
		/// It must therefore fail only on things that would fail again. Waiting
		/// for the engine to be ready is the business of Frame, not of Setup.
		[[nodiscard]] virtual bool Setup() = 0;

		/// Once per Present, only while running.
		virtual void Frame() {}

		/// Releases everything Setup acquired, and leaves the engine as it was
		/// found. Must be callable after a failed Setup, because a Setup that
		/// gave up halfway still has something to give back.
		virtual void Shutdown() = 0;
	};
}
