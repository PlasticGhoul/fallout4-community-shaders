// One oversized triangle that covers the screen, built from the vertex index
// alone. No vertex buffer, no input layout, three vertices - and no seam down
// the middle, which two triangles would have.
//
// Shared by every feature that draws a full-screen pass, which is why it ships
// in the base rather than in any one feature's addon: a player who installs
// only the base still has to have it the moment the first addon arrives.

struct VertexOutput
{
	float4 position: SV_POSITION;
	float2 uv: TEXCOORD0;
};

VertexOutput main(uint id : SV_VertexID)
{
	VertexOutput output;

	// id 0 -> (0,0), id 1 -> (2,0), id 2 -> (0,2). The triangle reaches twice
	// as far as the screen in each direction, and the rasteriser clips it.
	output.uv = float2((id << 1) & 2, id & 2);

	// uv (0,0) is the top left corner, so y runs the other way from clip space.
	output.position = float4(output.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);

	return output;
}
