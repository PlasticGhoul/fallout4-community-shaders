#include "Features/FrameCounter.h"

namespace Features
{
	bool FrameCounter::Setup()
	{
		_frames = 0;
		REX::INFO("FrameCounter: starting from zero");
		return true;
	}

	void FrameCounter::Frame()
	{
		++_frames;

		// Roughly every ten seconds at 60 fps: enough to show it is alive
		// without filling the log.
		if (_frames % kReportInterval == 0) {
			REX::INFO("FrameCounter: {} frames", _frames);
		}
	}

	void FrameCounter::Shutdown()
	{
		REX::INFO("FrameCounter: stopping after {} frames", _frames);
		_frames = 0;
	}
}
