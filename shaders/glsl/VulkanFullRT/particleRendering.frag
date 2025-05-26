/*
 * Abura Soba, 2025
 * 
 * Full Ray Tracing
 *
 * Particle Rendering pass
 *
 * Fragment shader
 */

 #version 460

#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_ray_query : enable

#include "../base/light.glsl"
#include "../base/3dgs.glsl"
#include "../base/utils.glsl"
#include "../base/define.glsl"

// Initialized with default value. Appropriate value will be transfered from application.
layout(constant_id = 0) const uint numOfLights = 1;	
layout(constant_id = 1) const uint numOfDynamicLights = 1;
layout(constant_id = 2) const uint numOfStaticLights = 1;
layout(constant_id = 3) const uint staticLightOffset = 1;
layout(constant_id = 4) const uint windowSizeX = 1;
layout(constant_id = 5) const uint windowSizeY = 1;

layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;
layout(binding = 2, set = 0) uniform uniformBuffer
{
	mat4 viewInverse;
	mat4 projInverse;
	//Light lights[numOfDynamicLights];
} uboDynamic;
layout(binding = 3, set = 0) uniform uniformBufferStatic
{
	Light lights[numOfStaticLights];
	Aabb aabb;
	float minTransmittance;
	float hitMinGaussianResponse;
	uint sphEvalDegree;
} uboStatic;

#if !BUFFER_REFERENCE
layout(std140, binding = 4, set = 0) readonly buffer ParticleDensities {
	ParticleDensity d[];
} particleDensities;
layout(std430, binding = 5, set = 0) readonly buffer ParticleSphCoefficients {
	float c[];
} particleSphCoefficients;	// [features_albedo(vec3), features_specular(float)]. uboStatic.particleRadiance
layout(std430, binding = 6, set = 0) buffer ParticleVisibility {
	float v[];
} particleVisibilities;
#endif

layout (location = 0) out vec4 outFragcolor;

#include "../base/gaussianfunctions.glsl"

// Global variable
RayPayload rayPayload;

/*** 3dgrt style ***/
void initializeRayPayload(){
	for(int i = 0; i < MAX_HIT_PER_TRACE; i++){
		rayPayload.hits[i].particleId = INVALID_PARTICLE_ID;
		rayPayload.hits[i].dist = INFINITE_DISTANCE;
	}
}

void compareAndSwapHitPayloadValue(inout RayHit hit, int idx) {
    const float dist = rayPayload.hits[idx].dist;
    if (hit.dist < dist) {
        rayPayload.hits[idx].dist = hit.dist;
        const uint id = rayPayload.hits[idx].particleId;
        rayPayload.hits[idx].particleId = hit.particleId;
        hit.dist = dist;
        hit.particleId = id;
    }
}

void trace(rayQueryEXT rayQuery, vec3 rayOri, vec3 rayDir, const float tmin, const float tmax){
	initializeRayPayload();
	rayQueryInitializeEXT(rayQuery, topLevelAS, gl_RayFlagsNoneEXT, 0xFF, rayOri, tmin, rayDir, tmax);

	while (rayQueryProceedEXT(rayQuery)) {
		if (rayQueryGetIntersectionTypeEXT(rayQuery, false) == gl_RayQueryCandidateIntersectionTriangleEXT) {
			uint primitiveId = rayQueryGetIntersectionPrimitiveIndexEXT(rayQuery, false);
			float hitT = rayQueryGetIntersectionTEXT(rayQuery, false);
			RayHit hit = RayHit(primitiveId / 20, hitT);

			if(hit.dist < rayPayload.hits[MAX_HIT_PER_TRACE - 1].dist) {
				compareAndSwapHitPayloadValue(hit, 0);
				compareAndSwapHitPayloadValue(hit, 1);
				compareAndSwapHitPayloadValue(hit, 2);
				compareAndSwapHitPayloadValue(hit, 3);
				compareAndSwapHitPayloadValue(hit, 4);
				compareAndSwapHitPayloadValue(hit, 5);
				compareAndSwapHitPayloadValue(hit, 6);
				compareAndSwapHitPayloadValue(hit, 7);
				compareAndSwapHitPayloadValue(hit, 8);
				compareAndSwapHitPayloadValue(hit, 9);
				compareAndSwapHitPayloadValue(hit, 10);
				compareAndSwapHitPayloadValue(hit, 11);
				compareAndSwapHitPayloadValue(hit, 12);
				compareAndSwapHitPayloadValue(hit, 13);
				compareAndSwapHitPayloadValue(hit, 14);
				compareAndSwapHitPayloadValue(hit, 15);

				if(rayPayload.hits[MAX_HIT_PER_TRACE - 1].dist <= hitT) {
					rayQueryConfirmIntersectionEXT(rayQuery);
				}
			}
		}
	}
}

void main()
{
	// set ray origin, direction
	const vec2 inUV = vec2(gl_FragCoord.xy) / vec2(windowSizeX, windowSizeY);
	vec2 d = inUV * 2.0f - 1.0f;	// pixel position in NDC

	vec4 rayOrigin = uboDynamic.viewInverse[3];
	vec4 target = uboDynamic.projInverse * vec4(d.x, d.y, 1.0f, 1.0f) ;	// (pixel position in EC) / Wc
	vec4 rayDirection = normalize(uboDynamic.viewInverse * vec4(target.xyz, 0.0f));

	/*** 3dgrt style ***/
	vec3 rayRadiance = vec3(0.0f);
	float rayTransmittance = 1.0f;
	float rayHitDistance = 0.0f;

#ifdef ENABLE_NORMALS
	vec3 rayNormal = vec3(0.0f);
#endif
#ifdef ENABLE_HIT_COUNTS
	uint rayHitsCount = 0;
#endif

	vec2 minMaxT = intersectAABB(uboStatic.aabb, rayOrigin.xyz, rayDirection.xyz);
	const float epsT = EPS_T;

	float rayLastHitDistance = max(0.0f, minMaxT.x - epsT);

	while((rayLastHitDistance <= minMaxT.y) && (rayTransmittance > uboStatic.minTransmittance)){
		// Gather k hits
		rayQueryEXT rayQuery;
		trace(rayQuery, rayOrigin.xyz, rayDirection.xyz, rayLastHitDistance + epsT, minMaxT.y + epsT);

		if(rayPayload.hits[0].particleId == INVALID_PARTICLE_ID){
			break;
		}

		// Process k hits
        for(int i = 0; i < MAX_HIT_PER_TRACE; i++){
			const RayHit rayHit = rayPayload.hits[nonuniformEXT(i)];

			if((rayHit.particleId != INVALID_PARTICLE_ID) && (rayTransmittance > uboStatic.minTransmittance)){
				const bool acceptedHit = processHit(
					rayOrigin.xyz,
					rayDirection.xyz,
					rayHit.particleId,
#if BUFFER_REFERENCE
					uboStatic.densityBufferDeviceAddress,
					uboStatic.sphCoefficientBufferDeviceAddress,
					uboStatic.visibilityBufferDeviceAddress,
#endif
					uboStatic.hitMinGaussianResponse,
					ALPHA_MIN_THRESHOLD,
					uboStatic.sphEvalDegree,
					rayTransmittance,
					rayRadiance,
					rayHitDistance
#if ENABLE_NORMALS
					,rayNormal
#endif
				);

				rayLastHitDistance = max(rayLastHitDistance, rayHit.dist);

#ifdef ENABLE_HIT_COUNTS
				rayHitsCount += acceptedHit ? 1 : 0;
#endif
			}
		}
	}

    outFragcolor = vec4(rayRadiance, 1.0f);
}