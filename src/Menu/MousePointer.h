#pragma once

namespace Menu
{
	/// Gives the overlay a pointer of its own, driven by mouse movement rather
	/// than by where the system cursor happens to be.
	///
	/// Measured on 1.11.240: while the game runs, something holds the system
	/// cursor inside the middle 1280x720 of a 2560x1440 screen and corrects a
	/// single pixel past that edge. It is not a clip rectangle - lifting that
	/// every frame changes nothing - and it does not move the cursor on its
	/// own, it only corrects. Reading the system cursor therefore cannot reach
	/// the edges of the screen, and an overlay that trusts it is unusable.
	///
	/// So the system cursor is parked in the middle of the window every frame
	/// and only the distance it travelled is taken. Starting from the middle,
	/// one frame of hand movement never reaches the rectangle's edges, so
	/// whatever enforces them never gets a turn. ImGui draws the pointer
	/// itself, which is what makes the parked cursor invisible.
	class MousePointer
	{
	public:
		struct Point
		{
			float x{ 0.0f };
			float y{ 0.0f };
		};

		/// Starts driving. Remembers where the system cursor was, so that
		/// Release can hand it back untouched.
		void Acquire(void* a_window) noexcept;

		/// One frame's worth. Returns where the overlay's pointer now is, in
		/// client coordinates.
		[[nodiscard]] Point Update() noexcept;

		/// Stops driving and puts the system cursor back.
		void Release() noexcept;

		[[nodiscard]] bool IsActive() const noexcept { return _active; }

	private:
		void* _window{ nullptr };

		std::int32_t _entryX{ 0 };
		std::int32_t _entryY{ 0 };

		Point _pointer;
		bool _active{ false };
	};
}
