// Exponential height fog, drawn onto the finished HDR scene.
//
// One full-screen triangle (Fullscreen.hlsl), depth in, colour and opacity
// out, blended with SRC_ALPHA / INV_SRC_ALPHA by the blend state. The
// integral is Unreal's, as the Skyrim version carries it; the fog colour is
// the game's own for this pixel, rebuilt from fogState the way the composite
// builds it, so that our layer and the game's distance fog agree in hue.
//
// Everything is relative to the camera. World coordinates in Fallout 4 run
// to eighty thousand, and a float has seven digits; only the height needs
// the camera's z, and that is one number.

Texture2D<float> DepthTexture : register(t0);

cbuffer PerFrame : register(b1)
{
	float4 CameraForward;   // xyz forward, w camera height
	float4 CameraRight;     // xyz right / sx, w near plane
	float4 CameraUp;        // xyz up / sy, w sky distance
	float4 SunDirection;    // xyz towards the sun, w sun inscattering
	float4 SunColor;        // rgb, w anisotropy
	float4 FogRange;        // x near, y far, z power, w clamp - fogState.rangeData.xy, power, clamp
	float4 FogHeightRange;  // fogState.highLowRangeData: near mid, near range, far mid, far range
	float4 FogNearLow;      // rgb
	float4 FogNearHigh;     // rgb
	float4 FogFarLow;       // rgb
	float4 FogFarHigh;      // rgb
	float4 Params;          // density, height, heightFalloff, startDistance
	float4 Screen;          // xy size, zw one over size
};

struct PixelInput
{
	float4 position: SV_POSITION;
	float2 uv: TEXCOORD0;
};

float HenyeyGreenstein(float cosTheta, float g)
{
	float g2 = g * g;
	float denominator = 1.0 + g2 - 2.0 * g * cosTheta;
	return (1.0 - g2) / (4.0 * 3.14159265 * pow(max(denominator, 1e-4), 1.5));
}

// A ramp that is zero at the bottom of a band and one at its top, the band
// given as its middle and its width - the shape the weather's fog height
// data has. Whether the band is centred on the middle or starts there is
// what FOG_DEBUG_COLOR settles against the game's own fog.
float HeightRamp(float height, float middle, float range)
{
	float width = max(range, 1.0);
	return saturate((height - (middle - 0.5 * width)) / width);
}

// The game's fog colour at this distance and height, as the composite mixes
// it: a distance ramp between near and far with a power, capped by the
// clamp, chooses along near/far; a height ramp, itself blended by distance
// between its near and far pair, chooses between the low and high colours.
float3 VanillaFogColor(float distance, float height)
{
	float nearDistance = FogRange.x;
	float farDistance = FogRange.y;
	float distanceFactor = saturate((distance - nearDistance) / max(farDistance - nearDistance, 1.0));
	float distancePow = pow(distanceFactor, FogRange.z);
	float fogIntensity = min(distancePow, FogRange.w);

	float nearHeight = HeightRamp(height, FogHeightRange.x, FogHeightRange.y);
	float farHeight = HeightRamp(height, FogHeightRange.z, FogHeightRange.w);
	float heightBlend = lerp(nearHeight, farHeight, distanceFactor);

	float3 low = lerp(FogNearLow.rgb, FogFarLow.rgb, fogIntensity);
	float3 high = lerp(FogNearHigh.rgb, FogFarHigh.rgb, fogIntensity);
	return lerp(low, high, heightBlend);
}

float4 main(PixelInput input) : SV_TARGET0
{
	const float depth = DepthTexture.Load(int3(input.position.xy, 0));

	// Pixel to NDC; y up, as clip space has it and Fullscreen.hlsl's uv does not.
	const float2 ndc = float2(input.uv.x * 2.0 - 1.0, 1.0 - input.uv.y * 2.0);
	const float3 ray = CameraForward.xyz + ndc.x * CameraRight.xyz + ndc.y * CameraUp.xyz;

	// z = 1 - near / d along forward; the sky reads as one and gets the
	// horizon distance instead.
	const float nearPlane = CameraRight.w;
	const float skyDistance = CameraUp.w;
	const float viewDepth = depth >= 0.99999 ? skyDistance : nearPlane / max(1.0 - depth, 1e-6);

	const float3 relative = ray * viewDepth;
	const float rayLength = length(relative);
	const float3 viewDirection = relative / max(rayLength, 1e-4);
	const float cameraHeight = CameraForward.w;
	const float pixelHeight = cameraHeight + relative.z;

	const float density = Params.x * 0.001;
	const float fogHeight = Params.y;
	const float heightFalloff = Params.z * 0.001;
	const float startDistance = Params.w;

	if (density <= 0.0)
		discard;

	// Unreal's exponential height fog line integral.
	float rayOriginTerms = density * exp2(-heightFalloff * max(cameraHeight - fogHeight, 0.0));
	float integralLength = rayLength;
	float rayDirectionZ = relative.z;

	if (startDistance > 0.0) {
		const float excludeDistance = min(startDistance, rayLength);
		const float excludeTime = excludeDistance / max(rayLength, 1e-4);
		const float cameraToExclusionZ = excludeTime * relative.z;
		const float exclusionZ = cameraHeight + cameraToExclusionZ;
		integralLength = (1.0 - excludeTime) * rayLength;
		rayDirectionZ = relative.z - cameraToExclusionZ;
		rayOriginTerms = density * exp2(-heightFalloff * max(exclusionZ - fogHeight, 0.0));
	}

	const float falloff = heightFalloff * rayDirectionZ;
	const float lineIntegral = (1.0 - exp2(-falloff)) / falloff;
	const float lineIntegralTaylor = 0.69314718056 - 0.24022650695 * falloff;
	const float integral = rayOriginTerms * (abs(falloff) > 0.01 ? lineIntegral : lineIntegralTaylor) * integralLength;
	const float transmittance = saturate(exp2(-integral));
	const float opacity = 1.0 - transmittance;

	float3 color = VanillaFogColor(rayLength, pixelHeight);

	// The sun's glow through the fog: a Henyey-Greenstein lobe around it.
	const float sunInscattering = SunDirection.w;
	if (sunInscattering > 0.0) {
		const float cosTheta = dot(SunDirection.xyz, viewDirection);
		const float phase = HenyeyGreenstein(cosTheta, SunColor.w);
		color += SunColor.rgb * phase * sunInscattering;
	}

	return float4(color, opacity);
}
