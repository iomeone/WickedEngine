#define DISABLE_TRANSPARENT_SHADOWMAP
#define DISABLE_ENVMAPS
#include "objectHF.hlsli"

float4 main(PixelInputType input) : SV_TARGET
{
	OBJECT_PS_DITHER

	OBJECT_PS_MAKE

	OBJECT_PS_COMPUTETANGENTSPACE

	color.a = 1;

	//NORMALMAP
	float2 bumpColor0=0;
	float2 bumpColor1=0;
	float2 bumpColor2=0;
	bumpColor0 = 2.0f * xNormalMap.Sample(sampler_objectshader,UV - g_xMat_texMulAdd.ww).rg - 1.0f;
	bumpColor1 = 2.0f * xNormalMap.Sample(sampler_objectshader,UV + g_xMat_texMulAdd.zw).rg - 1.0f;
	bumpColor2 = xWaterRipples.Sample(sampler_objectshader,ScreenCoord).rg;
	bumpColor= float3( bumpColor0+bumpColor1+bumpColor2,1 )  * g_xMat_refractionIndex;
	surface.N = normalize(lerp(surface.N, mul(normalize(bumpColor), TBN), g_xMat_normalMapStrength));
	bumpColor *= g_xMat_normalMapStrength;

	//REFLECTION
	float2 RefTex = float2(1, -1)*input.ReflectionMapSamplingPos.xy / input.ReflectionMapSamplingPos.w / 2.0f + 0.5f;
	float4 reflectiveColor = xReflection.SampleLevel(sampler_linear_mirror,RefTex+bumpColor.rg,0);
		
	
	//REFRACTION 
	float2 perturbatedRefrTexCoords = ScreenCoord.xy + bumpColor.rg;
	float refDepth = (texture_lineardepth.Sample(sampler_linear_mirror, ScreenCoord));
	float3 refractiveColor = xRefraction.SampleLevel(sampler_linear_mirror, perturbatedRefrTexCoords, 0).rgb;
	float mod = saturate(0.05*(refDepth - lineardepth));
	refractiveColor = lerp(refractiveColor, surface.baseColor.rgb, mod).rgb;
		
	//FRESNEL TERM
	float3 fresnelTerm = F_Fresnel(surface.f0, surface.NdotV);
	surface.albedo.rgb = lerp(refractiveColor, reflectiveColor.rgb, fresnelTerm);

	OBJECT_PS_LIGHT_FORWARD

	OBJECT_PS_LIGHT_END

	//SOFT EDGE
	float fade = saturate(0.3 * abs(refDepth - lineardepth));
	color.a *= fade;

	OBJECT_PS_FOG

	OBJECT_PS_OUT_FORWARD_SIMPLE
}