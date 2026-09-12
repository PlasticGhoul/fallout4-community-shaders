// Cloud shadows, multiplied onto the engine's two direct light targets.
//
// One full-screen triangle (Fullscreen.hlsl). The blend state is
// ZERO / SRC_COLOR, so what this returns is the factor the light is
// multiplied by; the shader itself never sees the light. Same arrangement
// as ScreenSpaceShadows/Modulate.hlsl, and it runs in the same phase.
//
// The coverage cubemap is A8: the engine's own cloud draws, repeated into
// five faces of our cubemap and composited by alpha alone. A pixel's world
// position comes from the depth buffer and the camera rows (F3); the
// direction into the cubemap is where the sun's ray through that point
// meets a spherical cloud shell - the template's projection, rearranged in
// CloudProjection.cpp so that the planet radius squared never appears.
//
// The "Debug View" setting, carried in CameraUp.w:
//   1  coverage   the sampled coverage, black to white
//   2  direction  normalize(direction) * 0.5 + 0.5
//   3  cube       the six faces of the coverage map, three by two:
//                 +X -X +Y over -Y +Z -Z, each face as D3D lays it out
// All are drawn with a plain blend state onto RT_058 alone.

TextureCube<float4> Coverage : register(t0);
Texture2D<float> DepthTexture : register(t1);
SamplerState LinearClamp : register(s0);

cbuffer PerFrame : register(b1)
{
	float4 CameraForward;  // xyz forward, w unused
	float4 CameraRight;    // xyz right / sx, w near plane
	float4 CameraUp;       // xyz up / sy, w debug view
	float4 SunDirection;   // xyz towards the sun, w opacity
	float4 Params;         // x cloud height (units), y planet radius (units), zw unused
};

struct PixelInput
{
	float4 position: SV_POSITION;
	float2 uv: TEXCOORD0;
};

struct PixelOutput
{
	float4 diffuse: SV_TARGET0;
	float4 specular: SV_TARGET1;
};

// CloudProjection::CloudSampleDirection, line for line, in float.
float3 CloudSampleDirection(float3 relative, float3 toSun, float planetRadius, float cloudHeight)
{
	const float dotUS = relative.x * toSun.x + relative.y * toSun.y + (relative.z + planetRadius) * toSun.z;
	const float lengthSquared = dot(relative, relative);
	const float c = lengthSquared + 2.0 * planetRadius * (relative.z - cloudHeight) - cloudHeight * cloudHeight;
	const float discriminant = max(dotUS * dotUS - c, 0.0);
	const float t = -dotUS + sqrt(discriminant);
	return relative + toSun * t;
}

PixelOutput Output(float3 value)
{
	PixelOutput output;
	// Alpha stays at one: the blend state multiplies alpha by SRC_ALPHA.
	output.diffuse = float4(value, 1.0);
	output.specular = float4(value, 1.0);
	return output;
}

// The direction a texel of face `face` at (s, t) in [-1, 1] stands for, per
// the D3D cube face orientation table; t runs down the texture like v.
float3 CubeDirection(int face, float s, float t)
{
	if (face == 0)
		return float3(1.0, -t, -s);
	if (face == 1)
		return float3(-1.0, -t, s);
	if (face == 2)
		return float3(s, 1.0, t);
	if (face == 3)
		return float3(s, -1.0, -t);
	if (face == 4)
		return float3(s, -t, 1.0);
	return float3(-s, -t, -1.0);
}

PixelOutput main(PixelInput input)
{
	const float depth = DepthTexture.Load(int3(input.position.xy, 0));
	const int debugView = (int)(CameraUp.w + 0.5);

	if (debugView == 3) {
		const float2 cell = float2(input.uv.x * 3.0, input.uv.y * 2.0);
		const int face = (int)floor(cell.x) + 3 * (int)floor(cell.y);
		const float2 st = frac(cell) * 2.0 - 1.0;
		const float value = Coverage.SampleLevel(LinearClamp, CubeDirection(face, st.x, st.y), 0).a;
		// A thin frame around each face, so the grid is readable.
		const float edge = max(abs(st.x), abs(st.y)) > 0.98 ? 0.5 : 0.0;
		return Output(float3(value, value + edge, value));
	}

	// Sky: no surface, no shadow.
	if (depth >= 0.9999)
		return Output(debugView == 0 ? 1.0.xxx : 0.0.xxx);

	const float2 ndc = float2(input.uv.x * 2.0 - 1.0, 1.0 - input.uv.y * 2.0);
	const float3 ray = CameraForward.xyz + ndc.x * CameraRight.xyz + ndc.y * CameraUp.xyz;
	const float nearPlane = CameraRight.w;
	const float viewDepth = nearPlane / max(1.0 - depth, 1e-6);
	const float3 relative = ray * viewDepth;

	const float3 direction = CloudSampleDirection(relative, SunDirection.xyz, Params.y, Params.x);
	const float coverage = Coverage.SampleLevel(LinearClamp, direction, 0).a;
	const float factor = saturate(1.0 - coverage * SunDirection.w);

	if (debugView == 1)
		return Output(coverage.xxx);
	if (debugView == 2)
		return Output(normalize(direction) * 0.5 + 0.5);

	return Output(factor.xxx);
}
