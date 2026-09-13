// One frame's sky direction into the probe grid: for every cell, was the
// cell's centre lit from that direction, as the occlusion depth map says;
// folded into the cell's SH as a running mean over the frames since the
// cell came into view. The template's UpdateProbesCS without the shadow
// cascade part. Dispatched (32, 32, 128) from kBeforeComposite.
#include "SkylightingCommon.hlsli"

Texture2D<float> OcclusionDepth : register(t0);
SamplerComparisonState Comparison : register(s0);

RWTexture3D<sh2> Probes : register(u0);
RWTexture3D<uint> AccumFrames : register(u1);

cbuffer PerFrame : register(b1)
{
	Skylighting::Settings Settings;
	float4 CameraForward;  // unused here, shared layout with the pass
	float4 CameraRight;
	float4 CameraUp;
	float4 Params;
};

static const uint kFadeInFrames = 15;

[numthreads(8, 8, 1)] void main(uint3 dtid : SV_DispatchThreadID) {
	const uint3 cellID = Skylighting::LogicalCell(dtid, Settings.ArrayOrigin.xyz);
	const uint3 validMin = (uint3)max(int3(0, 0, 0), Settings.ValidMargin.xyz);
	const uint3 validMax = Skylighting::ARRAY_DIM - 1 + (uint3)min(int3(0, 0, 0), Settings.ValidMargin.xyz);
	const bool isValid = all(cellID >= validMin) && all(cellID <= validMax);

	float3 cellCentre = cellID + 0.5 - Skylighting::ARRAY_DIM / 2;
	cellCentre = cellCentre / Skylighting::ARRAY_DIM * Skylighting::ARRAY_SIZE + Settings.PosOffset.xyz;

	// Through the same matrix the casters were drawn with: clip x, y in
	// [-1, 1], depth in [0, 1]. The map's v runs down while clip y runs up.
	const float4 clip = mul(Settings.OcclusionViewProj, float4(cellCentre, 1.0));
	const float2 occlusionUV = float2(clip.x * 0.5 + 0.5, 0.5 - clip.y * 0.5);

	if (all(occlusionUV > 0.0) && all(occlusionUV < 1.0)) {
		const uint accumFrames = isValid ? (AccumFrames[dtid] + 1) : 1;
		// 1 where the cell centre is nearer the sky than what the casters
		// drew there: the sky is visible from this direction.
		const float visibility = OcclusionDepth.SampleCmpLevelZero(Comparison, occlusionUV, clip.z);
		sh2 occlusionSH = SphericalHarmonics::Scale(SphericalHarmonics::Evaluate(-Settings.OcclusionDir.xyz), visibility * 4.0 * Math::PI);
		if (isValid) {
			const float lerpFactor = rcp((float)accumFrames);
			sh2 previous = Skylighting::UNIT_SH;
			if (accumFrames > 1)
				previous += (Probes[dtid] - Skylighting::UNIT_SH) * kFadeInFrames / min(kFadeInFrames, accumFrames - 1);
			occlusionSH = lerp(previous, occlusionSH, lerpFactor);
		}
		occlusionSH = lerp(Skylighting::UNIT_SH, occlusionSH, min(kFadeInFrames, accumFrames) / (float)kFadeInFrames);
		Probes[dtid] = occlusionSH;
		AccumFrames[dtid] = accumFrames;
	} else if (!isValid) {
		Probes[dtid] = Skylighting::UNIT_SH;
		AccumFrames[dtid] = 0;
	}
}
