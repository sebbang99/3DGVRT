/*
 * Sogang Univ, Graphics Lab, 2024
 *
 * Abura Soba, 2025
 *
 * Define.h
 *
 */

#pragma once

#if defined(VK_USE_PLATFORM_ANDROID_KHR)
#define START_FRAME 300
#define MEASURE_FRAME 500
#else
#define START_FRAME 2000
#define MEASURE_FRAME 2000
#endif

/* cameras */
#define QUATERNION_CAMERA true
#define LOAD_NERF_CAMERA true
#define CAMERA_FILE "transforms_val.json"
#define FOV_Y 39.6f
#define NEAR_PLANE 0.005f
#define FAR_PLANE 20.00f
// quaternion camera
#define CAM_MOVE_SPEED 0.15f
#define CAM_ROTATION_SPEED 0.5f


#define ASSET 3
#define LOAD_GLTF 0

 // ---------- split blas ---------- //
#define SPLIT_BLAS 0		// This macro should be managed with 3dgs.glsl
#define NUMBER_OF_CELLS_PER_LONGEST_AXIS 20
#define SCENE_EPSILON 1e-4f
#define ONE_VERTEX_BUFFER false

// ---------- compute pipeline ray query ---------- //
#define RAY_QUERY 0
#define TB_SIZE_X 1	// Should be managed with define.glsl
#define TB_SIZE_Y 2	// Should be managed with define.glsl

#define MULTIQUEUE 0	// 0 is Default
#define TIMER_CORRECTION 1
#define TEXTURE_COMPRESSION 0
#define ENABLE_HIT_COUNTS 0	// Should be managed with 3dgs.glsl. Only use when the RAY_QUERY is 0.
#define EVAL_QUALITY 1

#define USE_ANIMATION 0 // 0 is Default

#define N_IS_UP		// Should be managed with 3DGRT Asset Num.
//#define Y_IS_UP

#if ASSET == 0
#define ASSET_PATH "3DGRTModels/lego/"
#define PLY_FILE "lego.ply"

#elif ASSET == 1
#define ASSET_PATH "3DGRTModels/bonsai/"
#define PLY_FILE "bonsai.ply"
#undef LOAD_NERF_CAMERA
#define LOAD_NERF_CAMERA false

#elif ASSET == 2
#define ASSET_PATH "3DGRTModels/chair/"
#define PLY_FILE "chair.ply"

#elif ASSET == 3
#define ASSET_PATH "3DGRTModels/hotdog/"
#define PLY_FILE "hotdog.ply"

#elif ASSET == 4
#define ASSET_PATH "3DGRTModels/flowers/"
#define PLY_FILE "flowers.ply"
#endif

#if TEXTURE_COMPRESSION
#undef ASSET_PATH
#if ASSET == 0
#define ASSET_PATH "models/suntemple_etc1s/SunTemple.gltf"

#elif ASSET == 1
#define ASSET_PATH "models/sponza_etc1s/Sponza.gltf"

#elif ASSET == 4
#define ASSET_PATH "models/bistroexterior_etc1s/BistroExterior.gltf"
#endif
#endif

#define CUBEMAP_TEXTURE_PATH "cubeMapTextures/blueSky.ktx"

#if ASSET == 0
#define NUM_OF_LIGHTS_SUPPORTED 1
#define NUM_OF_DYNAMIC_LIGHTS 1	
#define NUM_OF_STATIC_LIGHTS 1
#define STATIC_LIGHT_OFFSET 0
#elif ASSET == 1
#define NUM_OF_LIGHTS_SUPPORTED 1
#define NUM_OF_DYNAMIC_LIGHTS 1		
#define NUM_OF_STATIC_LIGHTS 1		// for convenience
#define STATIC_LIGHT_OFFSET 0
#elif ASSET == 4
#define NUM_OF_LIGHTS_SUPPORTED 1
#define NUM_OF_DYNAMIC_LIGHTS 1	 // temporarily assigned value
#define NUM_OF_STATIC_LIGHTS 1	// temporarily assigned value
#define STATIC_LIGHT_OFFSET 0
#else
#define NUM_OF_LIGHTS_SUPPORTED 1
#define NUM_OF_DYNAMIC_LIGHTS 1		// for convenience
#define NUM_OF_STATIC_LIGHTS 1
#define STATIC_LIGHT_OFFSET 0
#endif

/*** 3DGS ***/
#define BUFFER_REFERENCE false		// This macro should be managed with 3dgs.glsl
#define NUM_OF_GAUSSIANS 1024	// This macro should be managed with 3dgs.glsl
#define MAX_N_FEATURES 3
#define SPECULAR_DIMENSION 3 * ((MAX_N_FEATURES + 1) * (MAX_N_FEATURES + 1) - 1)