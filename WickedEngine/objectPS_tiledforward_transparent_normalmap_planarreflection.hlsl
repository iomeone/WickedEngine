#define DISABLE_TRANSPARENT_SHADOWMAP
#include "objectHF.hlsli"

float4 main(PixelInputType input) : SV_TARGET
{
	OBJECT_PS_DITHER

	OBJECT_PS_MAKE

	OBJECT_PS_COMPUTETANGENTSPACE

	OBJECT_PS_NORMALMAPPING

	OBJECT_PS_SPECULARANTIALIASING

	OBJECT_PS_REFRACTION

	OBJECT_PS_LIGHT_TILED

	OBJECT_PS_PLANARREFLECTIONS

	OBJECT_PS_LIGHT_END

	OBJECT_PS_FOG

	OBJECT_PS_OUT_FORWARD_SIMPLE
}
