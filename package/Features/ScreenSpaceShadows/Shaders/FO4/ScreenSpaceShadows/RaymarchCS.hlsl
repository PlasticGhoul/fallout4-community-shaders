// Screen space contact shadows, Bend Studio's sweep.
//
// SAMPLE_COUNT is a compile-time constant the caller defines: the shader sizes
// a groupshared array from it. WAVE_SIZE is not a define from outside -
// bend_sss_gpu.hlsli sets it to 64 for itself, and BendDispatch.cpp passes the
// same 64 to BuildDispatchList. The two have to agree and neither reads the
// other.

// Relative to this file, not to the shader root: Shader::LoadSource splices
// with canonical.parent_path() / target, and both files sit in this directory.
#include "bend_sss_gpu.hlsli"

// FO4_DS_002 is R24G8_TYPELESS, and DepthStencilTarget::srViewDepth hands it
// over as R24_UNORM_X8_TYPELESS, so depth arrives as a unorm float. This is the
// branch the Skyrim original kept for the game's own depth buffer; its
// TERRAIN_BLENDING alternative described a texture this port does not have, and
// is gone rather than carried along dead.
Texture2D<unorm float> DepthTexture : register(t0);

RWTexture2D<unorm float> OutputTexture : register(u0);

// Point sampling, clamped to a border of the far depth value, so that a ray
// leaving the screen reads "nothing in the way" rather than whatever the edge
// pixel happens to hold.
SamplerState PointBorderSampler : register(s0);

cbuffer PerFrame : register(b1)
{
	float4 LightCoordinate;
	int2 WaveOffset;

	float FarDepthValue;
	float NearDepthValue;

	float2 InvDepthTextureSize;
	float2 DynamicRes;

	float SurfaceThickness;
	float BilinearThreshold;
	float ShadowContrast;
	float Padding;
};

[numthreads(WAVE_SIZE, 1, 1)] void main(
	int3 groupID : SV_GroupID,
	int groupThreadID : SV_GroupThreadID) {
	DispatchParameters parameters;
	parameters.SetDefaults();

	parameters.LightCoordinate = LightCoordinate;
	parameters.WaveOffset = WaveOffset;

	// Read from the buffer rather than hard-coded, as the Skyrim version has
	// them: which end of the depth range is near is unestablished for Fallout 4,
	// and this way settling it is a line on the CPU side rather than a shader
	// edit and a game restart.
	parameters.FarDepthValue = FarDepthValue;
	parameters.NearDepthValue = NearDepthValue;

	parameters.InvDepthTextureSize = InvDepthTextureSize;
	parameters.DepthTexture = DepthTexture;
	parameters.OutputTexture = OutputTexture;
	parameters.PointBorderSampler = PointBorderSampler;

	parameters.SurfaceThickness = SurfaceThickness;
	parameters.BilinearThreshold = BilinearThreshold;
	parameters.ShadowContrast = ShadowContrast;

	parameters.DynamicRes = DynamicRes;
	parameters.UsePrecisionOffset = true;

	WriteScreenSpaceShadow(parameters, groupID, groupThreadID);
}
