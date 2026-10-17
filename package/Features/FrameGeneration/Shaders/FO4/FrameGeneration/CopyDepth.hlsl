// The depth for DLSS Frame Generation: the rectangle of DS_002 that holds
// this frame into an R32_FLOAT texture D3D12 can open. A depth-stencil
// texture cannot be shared between the devices, a plain float one can.
Texture2D<float> DepthInput : register(t0);

RWTexture2D<float> DepthOutput : register(u0);

cbuffer CopyDepth : register(b0)
{
	uint2 Size;
	uint2 Padding;
};

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	if (any(dispatchID.xy >= Size))
		return;
	DepthOutput[dispatchID.xy] = DepthInput[dispatchID.xy];
}
