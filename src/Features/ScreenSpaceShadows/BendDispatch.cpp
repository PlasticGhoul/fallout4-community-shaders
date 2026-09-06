#include "Features/ScreenSpaceShadows/BendDispatch.h"

// Third-party source, kept verbatim under Apache-2.0, and excluded from
// clang-format by name in .pre-commit-config.yaml. bend_sss_cpu.h:90 assigns
// an int to a float, which /W4 /WX will not have. Suppressed here and only
// here, naming the header, rather than by relaxing the option for our own
// code. Exactly one warning, checked by building once without this: the two
// further numbers the Skyrim version suppresses are not raised.
#pragma warning(push)
#pragma warning(disable: 4244)
#include "Features/ScreenSpaceShadows/bend_sss_cpu.h"
#pragma warning(pop)

namespace Features::Bend
{
	DispatchPlan BuildPlan(const float a_lightProjection[4], const int a_viewportSize[2])
	{
		// BuildDispatchList takes its arguments by non-const pointer without
		// writing through them.
		float light[4] = {
			a_lightProjection[0],
			a_lightProjection[1],
			a_lightProjection[2],
			a_lightProjection[3]
		};

		int viewport[2] = { a_viewportSize[0], a_viewportSize[1] };
		int minBounds[2] = { 0, 0 };
		int maxBounds[2] = { a_viewportSize[0], a_viewportSize[1] };

		// The wave size is 64 because bend_sss_gpu.hlsli defines WAVE_SIZE 64
		// for itself. The two have to agree and neither reads the other.
		const auto list =
			::Bend::BuildDispatchList(light, viewport, minBounds, maxBounds, false, 64);

		DispatchPlan plan;
		plan.lightCoordinate[0] = list.LightCoordinate_Shader[0];
		plan.lightCoordinate[1] = list.LightCoordinate_Shader[1];
		plan.lightCoordinate[2] = list.LightCoordinate_Shader[2];
		plan.lightCoordinate[3] = list.LightCoordinate_Shader[3];

		plan.count = list.DispatchCount < 8 ? list.DispatchCount : 8;
		for (int i = 0; i < plan.count; ++i) {
			plan.dispatches[i].waveCount[0] = list.Dispatch[i].WaveCount[0];
			plan.dispatches[i].waveCount[1] = list.Dispatch[i].WaveCount[1];
			plan.dispatches[i].waveCount[2] = list.Dispatch[i].WaveCount[2];
			plan.dispatches[i].waveOffset[0] = list.Dispatch[i].WaveOffset_Shader[0];
			plan.dispatches[i].waveOffset[1] = list.Dispatch[i].WaveOffset_Shader[1];
		}

		return plan;
	}
}
