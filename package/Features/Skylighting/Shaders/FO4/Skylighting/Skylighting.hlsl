// The sky visibility of every pixel, as a factor on the ambient light.
//
// Drawn in kComposite onto RT_028 with ZERO / SRC_COLOR, so that the
// engine's own composite carries our factor onto its ambient term. Half
// resolution: RT_028 is 1280x720, the depth is read at the matching full
// resolution pixel.
//
// The "Debug View" setting, carried in CameraUp.w, draws onto the picture
// in kAfterOpaque instead:
//   1  visibility  the factor, black to white
//   2  occlusion   this frame's depth map from the sky, laid over the screen
//   3  grid        the grid's zenith visibility at the pixel, no normal
#include "../Common/Reconstruct.hlsli"
#include "SkylightingCommon.hlsli"

Texture3D<sh2> Probes : register(t0);
Texture2D<float> DepthTexture : register(t1);
Texture2D<float> OcclusionDepth : register(t2);
SamplerState LinearClamp : register(s0);

cbuffer PerFrame : register(b1)
{
	Skylighting::Settings Settings;
	float4 CameraForward;  // xyz forward, w unused
	float4 CameraRight;    // xyz right / sx, w near plane
	float4 CameraUp;       // xyz up / sy, w debug view
	float4 Params;         // x min visibility, y unused, zw full resolution size
};

struct PixelInput
{
	float4 position: SV_POSITION;
	float2 uv: TEXCOORD0;
};

// The HDR target is tone mapped after us; a value of one reads as white,
// so the debug views are drawn at a quarter to stay legible.
// Measured on 2026-09-13: the HDR scene averages 0.03 to 0.06 in the open,
// so a quarter was still ten times too bright and every view came out white.
static const float kDebugExposure = 0.05;

float3 PositionAt(float2 uv)
{
	const int2 pixel = int2(uv * Params.zw);
	const float depth = DepthTexture.Load(int3(pixel, 0));
	return Reconstruct::Position(uv, depth, CameraRight.w, CameraForward.xyz, CameraRight.xyz, CameraUp.xyz);
}

float4 main(PixelInput input) : SV_TARGET0
{
	const int debugView = (int)(CameraUp.w + 0.5);
	if (debugView == 2)
		return float4(OcclusionDepth.Sample(LinearClamp, input.uv).rrr * kDebugExposure, 1.0);

	const int2 pixel = int2(input.uv * Params.zw);
	const float depth = DepthTexture.Load(int3(pixel, 0));
	if (depth >= 0.9999)
		return float4((debugView == 0 ? 1.0 : kDebugExposure).xxx, 1.0);

	const float2 texel = 1.0 / Params.zw;
	const float3 position = Reconstruct::Position(input.uv, depth, CameraRight.w, CameraForward.xyz, CameraRight.xyz, CameraUp.xyz);
	const float3 normal = Reconstruct::Normal(position, PositionAt(input.uv + float2(texel.x, 0.0)), PositionAt(input.uv + float2(0.0, texel.y)));

	const sh2 sh = Skylighting::Sample(Probes, Settings, position, normal);
	const float fade = Skylighting::GetFadeOutFactor(position);
	const float minVisibility = Params.x;

	if (debugView == 3) {
		const float zenith = Skylighting::CosineLobeVisibility(sh, float3(0.0, 0.0, 1.0), fade);
		return float4(zenith.xxx * kDebugExposure, 1.0);
	}

	const float visibility = Skylighting::CosineLobeVisibility(sh, normal, fade);
	const float factor = lerp(minVisibility, 1.0, visibility);
	if (debugView == 1)
		return float4(factor.xxx * kDebugExposure, 1.0);
	return float4(factor.xxx, 1.0);
}
