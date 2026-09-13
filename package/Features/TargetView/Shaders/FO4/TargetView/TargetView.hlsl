// One engine render target laid over the finished picture, for looking at
// what a pass leaves behind. Drawn in kAfterOpaque, after the sky, so every
// pixel shows it - F4's cube view drew before the sky and was blind there.
Texture2D<float4> Source : register(t0);
SamplerState PointClamp : register(s0);

cbuffer PerFrame : register(b1)
{
	float4 Params;  // x channel (0 rgb, 1 r, 2 g, 3 b, 4 a), yzw unused
};

struct PixelInput
{
	float4 position: SV_POSITION;
	float2 uv: TEXCOORD0;
};

float4 main(PixelInput input) : SV_TARGET0
{
	const float4 value = Source.Sample(PointClamp, input.uv);
	const int channel = (int)(Params.x + 0.5);
	if (channel == 1)
		return float4(value.rrr, 1.0);
	if (channel == 2)
		return float4(value.ggg, 1.0);
	if (channel == 3)
		return float4(value.bbb, 1.0);
	if (channel == 4)
		return float4(value.aaa, 1.0);
	return float4(value.rgb, 1.0);
}
