#ifndef FO4CS_SKYLIGHTING_COMMON_HLSLI
#define FO4CS_SKYLIGHTING_COMMON_HLSLI

#include "../Common/SphericalHarmonics.hlsli"

// The template's Skylighting.hlsli, cut to what the Fallout 4 build uses:
// the grid sample and the diffuse evaluation. Constants are the same
// numbers as OcclusionProjection.h; the update shader and the pass both
// read this file so they cannot disagree.
namespace Skylighting
{
	static const uint3 ARRAY_DIM = uint3(256, 256, 128);
	static const float3 ARRAY_SIZE = 10000.0 * float3(1.0, 1.0, 0.5);
	static const float3 CELL_SIZE = ARRAY_SIZE / ARRAY_DIM;
	static const sh2 UNIT_SH = float4(sqrt(4.0 * Math::PI), 0.0, 0.0, 0.0);
	static const float EPSILON_WEIGHT_SUM = 1e-6;

	struct Settings
	{
		row_major float4x4 OcclusionViewProj;
		float4 OcclusionDir;  // xyz the direction the occlusion camera looks along, down
		float4 PosOffset;     // xyz cell origin relative to the eye
		uint4 ArrayOrigin;    // xyz ring buffer origin
		int4 ValidMargin;     // xyz previous cell - current cell
	};

	// Texture cell of a logical cell, and back.
	uint3 TextureCell(uint3 cellID, uint3 arrayOrigin)
	{
		return (cellID + arrayOrigin) % ARRAY_DIM;
	}

	uint3 LogicalCell(uint3 textureCell, uint3 arrayOrigin)
	{
		return (textureCell + ARRAY_DIM - arrayOrigin) % ARRAY_DIM;
	}

	float GetFadeOutFactor(float3 positionMS)
	{
		const float3 uvw = saturate(positionMS / ARRAY_SIZE + 0.5);
		const float3 dists = min(uvw, 1.0 - uvw);
		const float edgeDist = min(dists.x, min(dists.y, dists.z));
		return saturate(edgeDist * 20.0);
	}

	float CosineLobeVisibility(sh2 skylightingSH, float3 normal, float fadeOutFactor)
	{
		const float visibility = SphericalHarmonics::FuncProductIntegral(skylightingSH, SphericalHarmonics::EvaluateCosineLobe(normal)) / Math::PI;
		return lerp(1.0, saturate(visibility), fadeOutFactor);
	}

	// Eight cells trilinearly, each weighted by how much it lies in front
	// of the surface, after a half-cell push along the normal.
	sh2 Sample(Texture3D<sh2> probes, Settings settings, float3 positionMS, float3 normalWS)
	{
		positionMS += normalWS * CELL_SIZE * 0.5;
		const float3 adjusted = positionMS - settings.PosOffset.xyz;
		const float3 uvw = adjusted / ARRAY_SIZE + 0.5;
		if (any(uvw < 0.0) || any(uvw > 1.0))
			return UNIT_SH;
		const float3 cellCoord = uvw * ARRAY_DIM;
		const int3 cell000 = (int3)floor(cellCoord - 0.5);
		const float3 trilinear = cellCoord - 0.5 - cell000;
		sh2 sum = 0.0;
		float weightSum = 0.0;
		[unroll] for (int i = 0; i < 2; i++)
			[unroll] for (int j = 0; j < 2; j++)
				[unroll] for (int k = 0; k < 2; k++)
		{
			const int3 offset = int3(i, j, k);
			const int3 cellID = cell000 + offset;
			if (any(cellID < 0) || any((uint3)cellID >= ARRAY_DIM))
				continue;
			const float3 cellCentre = (cellID + 0.5 - ARRAY_DIM / 2) * CELL_SIZE;
			const float3 weights = 1.0 - abs(offset - trilinear);
			const float triW = weights.x * weights.y * weights.z;
			const float tangentW = dot(normalize(cellCentre - adjusted), normalWS) * 0.5 + 0.5;
			const float w = triW * tangentW;
			sum = SphericalHarmonics::Add(sum, SphericalHarmonics::Scale(probes[TextureCell((uint3)cellID, settings.ArrayOrigin.xyz)], w));
			weightSum += w;
		}
		if (weightSum <= EPSILON_WEIGHT_SUM)
			return UNIT_SH;
		return SphericalHarmonics::Scale(sum, rcp(weightSum));
	}
}

#endif
