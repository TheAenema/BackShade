// Ported by H.M //

#include "ReShade.fxh"

uniform float time < source = "timer"; >;
#define timer (time*.0005)

#define PI 3.1415926

float2 hash2(float2 p) { p=float2(dot(p,float2(127.1,311.7)),dot(p,float2(269.5,183.3))); return frac(sin(p)*43758.5453); }
float3x3 lookAt( float3 eye, float3 center, float3 up )
{
    float3 f = normalize(center - eye);
    float3 s = normalize(cross(f, up));
    float3 u = cross(s, f);
    return float3x3(s, u, -f);
}
float voronoiDistance( float2 pos )
{
    float2 p = floor(pos), f = frac(pos);
    float u = 0.5 * (sin(timer-0.5*PI) + 1.0);

    float2 res = float2(8.0, 0.0);
    for(int i = -1; i <= 1; i++)
        for(int k = -1; k <= 1; k++)
        {
            float2 b = float2(i, k);
            float2 r = b - f + hash2(p + b) * u;

            float d = dot(r, r);

            if(d < res.x){
                res.y = res.x;
                res.x = d;
            }
            else if(d < res.y){
                res.y = d;
            }
        }
    return res.y - res.x;
}
float3 render( float3 rayOri, float3 rayDir )
{
    float theta = 2.0 * (acos(0.5*rayDir.x) / PI) - 1.0;
    float phi = atan2(rayDir.y, rayDir.z) / PI;
    float2 uv = float2(theta, phi);

    float v = 0.0;
    for(float i = 0., a = .6, f = 8.; i < 3.; ++i, f*=2., a*=.6)
    {
        float v1 = 1.0 - smoothstep(0.0, 0.2, voronoiDistance(uv * f));
        float v2 = 1.0 - smoothstep(0.0, 0.2, voronoiDistance(uv * f * 0.5 + timer));
        float intensity = 0.5 * (cos(timer) + 1.0);
        v += a * (pow(v1 * (0.5 + v2), 2.0) + v1 * intensity + 0.1);
    }

    float3 c = float3(8.0, 2.5, 2.0);
    float3 col = pow(float3(v, v, v), c) * 2.0;

    return col * 1.2 + float3(0, 0, 0.1);
}

// Shader Code
float3 ImagePS(float4 vois : SV_Position, float2 texcoord : TexCoord) : SV_Target
{

	float2 iResolution = float2(BUFFER_WIDTH, BUFFER_HEIGHT);
	float2 uv = (2.0 * vois - iResolution.xy) / iResolution.y;
    float3 rayOri = float3(0.0, 0.0, 0.0);
    float3 rayTgt = float3(1.0, 0.0, 0.0);

    float3x3 viewMat = lookAt(rayOri, rayTgt, float3(0.0, 0.1, 0.0));
	float3 multiplied = mul(viewMat, float3(uv.x ,uv.y, -1.0));
    float3 rayDir = normalize(multiplied);

    float3 col = render(rayOri, rayDir);
	return col;
}

// Technique Code
technique Demo <
	ui_label = "Demo - Voronoi Noise";
	ui_tooltip ="Created by ikuto https://www.shadertoy.com/view/MlycRy";
	>
{
	pass
	{
		VertexShader = PostProcessVS;
		PixelShader = ImagePS;
	}
}
