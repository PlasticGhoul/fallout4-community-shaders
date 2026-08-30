// Replacement for the engine's simplest image space pass.
//
// The tint is not an effect, it is the evidence: subproject C is finished when
// a screenshot shows it. The pass reads one texture and writes one colour, so
// it needs to know nothing about the rest of the engine's bindings. Which slot
// this actually lands in is decided at runtime by the imagespace catalog.
Texture2D<float4> SourceTexture : register(t0);
SamplerState SourceSampler : register(s0);

static const float3 ProofTint = float3(1.0, 0.6, 0.6);

float4 main(float4 position : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET
{
	float4 source = SourceTexture.Sample(SourceSampler, uv);
	return float4(source.rgb * ProofTint, source.a);
}
