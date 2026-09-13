// A pixel's position and normal from the depth buffer and the camera's
// basis, the way F3 and F4 reconstruct it: the view ray from the pixel's
// place in the picture, its length from z = 1 - near / d. Everything is
// relative to the camera; world coordinates run to eighty thousand.
//
// The caller provides the depth texture and the constants; this only does
// the arithmetic, so that a pass in half resolution can sample the full
// resolution depth at whatever pixel it likes.
#ifndef FO4CS_RECONSTRUCT_HLSLI
#define FO4CS_RECONSTRUCT_HLSLI

namespace Reconstruct
{
	// uv in [0, 1] with y down (Fullscreen.hlsl), depth the raw buffer value.
	float3 ViewRay(float2 uv, float3 cameraForward, float3 cameraRightOverScale, float3 cameraUpOverScale)
	{
		const float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
		return cameraForward + ndc.x * cameraRightOverScale + ndc.y * cameraUpOverScale;
	}

	float ViewDepth(float depth, float nearPlane)
	{
		return nearPlane / max(1.0 - depth, 1e-6);
	}

	float3 Position(float2 uv, float depth, float nearPlane, float3 cameraForward, float3 cameraRightOverScale, float3 cameraUpOverScale)
	{
		return ViewRay(uv, cameraForward, cameraRightOverScale, cameraUpOverScale) * ViewDepth(depth, nearPlane);
	}

	// The normal from the positions of this pixel and the two neighbours to
	// its right and below, turned to face the camera. One pixel wide at
	// edges; good enough for a factor on the ambient light.
	float3 Normal(float3 position, float3 positionRight, float3 positionDown)
	{
		const float3 normal = normalize(cross(positionDown - position, positionRight - position));
		// The view ray points away from the camera; a normal facing it
		// points against the ray.
		return dot(normal, position) > 0.0 ? -normal : normal;
	}
}

#endif
