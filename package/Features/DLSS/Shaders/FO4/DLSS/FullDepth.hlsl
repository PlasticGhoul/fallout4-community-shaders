// The depth at display size, from the render rectangle of DS_002, for the
// passes after DLSS that read depth across the whole picture - the composite
// and the lens flare see it in place of the engine's. The linear copy goes to
// mip 0 of RT_039, which the engine's later effects read. After doodlum's
// fo4test upscaler (OverrideDepthCS.hlsl, GPL-3.0), unchanged in substance.
Texture2D<float> DepthInput : register(t0);

RWTexture2D<float> DepthOutput : register(u0);
RWTexture2D<float> LinearDepthOutput : register(u1);

cbuffer Upscaling : register(b0)
{
	uint2 ScreenSize;
	uint2 RenderSize;
	float4 CameraData;  // far, near, far - near, far * near
};

float GetScreenDepth(float depth)
{
	return (CameraData.w / (-depth * CameraData.z + CameraData.x));
}

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	if (any(dispatchID.xy >= ScreenSize))
		return;

	const float depth = DepthInput[dispatchID.xy * RenderSize / ScreenSize];

	DepthOutput[dispatchID.xy] = depth;
	LinearDepthOutput[dispatchID.xy] = GetScreenDepth(depth);
}
