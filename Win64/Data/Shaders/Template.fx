#include "ReShade.fxh"

// Shader Code
float3 ImagePS(float4 vois : SV_Position, float2 texcoord : TexCoord) : SV_Target
{
	float3 Display = tex2D(ReShade::BackBuffer, texcoord).rgb;
	return Display;
}

// Technique Code
technique Template <
	ui_label = "Template Shader";
	ui_tooltip ="A Simple Shader Template for BackShade";
	>
{
	pass
	{
		VertexShader = PostProcessVS;
		PixelShader = ImagePS;
	}
}
