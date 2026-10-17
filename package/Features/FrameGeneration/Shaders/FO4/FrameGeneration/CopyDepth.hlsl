// The depth for DLSS Frame Generation, in an R32_FLOAT texture D3D12 can
// open - a depth-stencil texture cannot be shared between the devices, a
// plain float one can. Written at the motion vectors' resolution, whatever
// the input is: DLSS-G reads depth and motion vectors as one pair, and while
// DLSS upscales, the depth bound at the capture point is the display-size one
// DLSS put in, not the render-size one the motion vectors match.
Texture2D<float> DepthInput : register(t0);

RWTexture2D<float> DepthOutput : register(u0);

cbuffer CopyDepth : register(b0)
{
	uint2 OutputSize;
	uint2 InputSize;
};

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	if (any(dispatchID.xy >= OutputSize))
		return;
	const uint2 source = min(dispatchID.xy * InputSize / OutputSize, InputSize - 1);
	DepthOutput[dispatchID.xy] = DepthInput[source];
}
