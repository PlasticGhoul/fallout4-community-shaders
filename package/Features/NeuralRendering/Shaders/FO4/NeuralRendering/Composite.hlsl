// Puts DLSS Neural Rendering's picture under the HUD. Where the finished
// picture and the one taken before the HUD agree, nothing was drawn on top:
// the neural picture goes there. Where they differ, the HUD, a fader or the
// overlay was drawn, and the finished picture stays. Both are copies of the
// same back buffer in the same format, so the comparison is exact.

Texture2D<float4> Final : register(t0);
Texture2D<float4> Hudless : register(t1);
Texture2D<float4> Neural : register(t2);
RWTexture2D<float4> Composed : register(u0);

cbuffer Composite : register(b0)
{
	uint2 Size;
	uint View;  // 0 the picture, 1 original on the left half, 2 kept pixels tinted
	uint Pad;
};

[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {
	if (any(id.xy >= Size)) {
		return;
	}
	const float4 finalColor = Final[id.xy];
	const bool kept = any(finalColor != Hudless[id.xy]);
	float4 color = kept ? finalColor : Neural[id.xy];
	if (View == 1 && id.x < Size.x / 2) {
		color = finalColor;
	} else if (View == 2 && kept) {
		color = lerp(finalColor, float4(1.0, 0.0, 1.0, 1.0), 0.5);
	}
	Composed[id.xy] = float4(color.rgb, 1.0);
}
