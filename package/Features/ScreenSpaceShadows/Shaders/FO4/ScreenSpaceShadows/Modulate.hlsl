// Multiplies the shadow mask onto the engine's two light targets.
//
// The multiplication itself is the blend state - ZERO / SRC_COLOR - so this
// shader only has to hand the same mask value to both outputs. Writing
// dest * src here would need the destination as an input, and FO4_RT_058 and
// FO4_RT_059 carry no unordered access view to read it through.

Texture2D<float> ShadowMask : register(t0);

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

PixelOutput main(PixelInput input)
{
	// Load rather than Sample: the mask is the same size as the targets, so
	// there is nothing to interpolate and no sampler to bind.
	const float shadow = ShadowMask.Load(int3(input.position.xy, 0));

	PixelOutput output;

	// Alpha stays at one. The blend state multiplies alpha by SRC_ALPHA, so
	// anything else here would quietly scale the targets' alpha as well.
	output.diffuse = float4(shadow, shadow, shadow, 1.0);
	output.specular = float4(shadow, shadow, shadow, 1.0);

	return output;
}
