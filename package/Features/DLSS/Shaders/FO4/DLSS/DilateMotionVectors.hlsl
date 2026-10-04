// The engine's motion vectors (RT_029), each replaced by the longest one in
// its 5x5 neighbourhood that lies nearer the camera - so a thin object moving
// across a far background carries its motion to its silhouette, where DLSS
// samples it. Only beyond 10240 units, where the vectors of distant geometry
// are too sparse to trust. After doodlum's fo4test upscaler
// (DilateMotionVectorCS.hlsl, GPL-3.0), unchanged in substance.
Texture2D<float2> MotionVectorInput : register(t0);
Texture2D<float> DepthInput : register(t1);

RWTexture2D<float2> MotionVectorOutput : register(u0);

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
	if (any(dispatchID.xy >= RenderSize))
		return;

	const float depth = DepthInput[dispatchID.xy];

	float2 motionVector = MotionVectorInput[dispatchID.xy];
	float2 longestMotionVector = motionVector;
	float maxMotionLengthSq = dot(motionVector, motionVector);

	if (GetScreenDepth(depth) > (4096 * 2.5)) {
		[unroll] for (int y = -2; y <= 2; y++)
		{
			[unroll] for (int x = -2; x <= 2; x++)
			{
				const int2 samplePos = int2(dispatchID.xy) + int2(x, y);
				if (any(samplePos < 0) || any(samplePos >= int2(RenderSize)))
					continue;

				const float neighborDepth = DepthInput[samplePos];
				if (neighborDepth < depth) {
					const float2 neighborMotionVector = MotionVectorInput[samplePos];
					const float motionLengthSq = dot(neighborMotionVector, neighborMotionVector);
					if (motionLengthSq > maxMotionLengthSq) {
						maxMotionLengthSq = motionLengthSq;
						longestMotionVector = neighborMotionVector;
					}
				}
			}
		}
	}

	MotionVectorOutput[dispatchID.xy] = longestMotionVector;
}
