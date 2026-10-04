// FSR 1's robust contrast adaptive sharpening, after DLSS, at display size.
// Taken from doodlum's fo4test upscaler (RCAS/RCAS.hlsl, GPL-3.0), which is
// based on https://github.com/RdenBlaauwen/RCAS-for-ReShade and AMD's
// ffx_fsr1.h. One change: the sharpness comes from the constant buffer
// instead of a define, so dragging the slider does not recompile the shader.
//
// RCAS uses a 5 tap filter in a cross pattern,
//    w                n
//  w 1 w  for taps  w m e
//    w                s
// and solves for the negative lobe weight 'w' that sharpens the most without
// clipping, then damps it where a highpass suggests noise rather than edges.
Texture2D<float3> Source : register(t0);
RWTexture2D<float3> Dest : register(u0);

cbuffer Upscaling : register(b0)
{
	uint2 ScreenSize;
	uint2 RenderSize;
	float4 CameraData;  // x sharpness, 0 to 1
};

float getRCASLuma(float3 rgb)
{
	return dot(rgb, float3(0.5, 1.0, 0.5));
}

[numthreads(8, 8, 1)] void main(uint3 DTid : SV_DispatchThreadID) {
	if (any(DTid.xy >= ScreenSize))
		return;

	const float3 e = Source.Load(int3(DTid.x, DTid.y, 0)).rgb;
	const float3 b = Source.Load(int3(DTid.x, DTid.y - 1, 0)).rgb;
	const float3 d = Source.Load(int3(DTid.x - 1, DTid.y, 0)).rgb;
	const float3 f = Source.Load(int3(DTid.x + 1, DTid.y, 0)).rgb;
	const float3 h = Source.Load(int3(DTid.x, DTid.y + 1, 0)).rgb;

	// Luma times 2.
	const float bL = getRCASLuma(b);
	const float dL = getRCASLuma(d);
	const float eL = getRCASLuma(e);
	const float fL = getRCASLuma(f);
	const float hL = getRCASLuma(h);

	// Noise detection.
	float nz = (bL + dL + fL + hL) * 0.25 - eL;
	const float range = max(max(max(bL, dL), max(hL, fL)), eL) - min(min(min(bL, dL), min(eL, fL)), hL);
	nz = saturate(abs(nz) * rcp(range));
	nz = -0.5 * nz + 1.0;

	// Min and max of ring.
	const float3 minRGB = min(min(b, d), min(f, h));
	const float3 maxRGB = max(max(b, d), max(f, h));

	// Immediate constants for peak range.
	const float2 peakC = float2(1.0, -4.0);

	// Limiters.
	const float3 hitMin = minRGB * rcp(4.0 * maxRGB);
	const float3 hitMax = (peakC.xxx - maxRGB) * rcp(4.0 * minRGB + peakC.yyy);
	const float3 lobeRGB = max(-hitMin, hitMax);
	float lobe = max(-0.1875, min(max(lobeRGB.r, max(lobeRGB.g, lobeRGB.b)), 0.0)) * CameraData.x;

	// Apply noise removal.
	lobe *= nz;

	// Resolve.
	const float rcpL = rcp(4.0 * lobe + 1.0);
	Dest[DTid.xy] = ((b + d + f + h) * lobe + e) * rcpL;
}
