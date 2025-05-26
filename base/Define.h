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

#define ASSET 0
#define LOAD_GLTF 0

#define RAY_QUERY 2	// 0 : RT, 1 : Compute RQ, 2 : Graphics RQ
#define TB_SIZE_X 1	// Should be managed with define.glsl
#define TB_SIZE_Y 2	// Should be managed with define.glsl

#define MULTIQUEUE 0	// 0 is Default
#define TIMER_CORRECTION 1
#define TEXTURE_COMPRESSION 1

#define USE_ANIMATION 0 // 0 is Default

#define N_IS_UP		// Should be managed with 3DGRT Asset Num.

#if ASSET == 0
#define ASSET_PATH "models/suntemple_simple/SunTemple.gltf"

#elif ASSET == 1
#define ASSET_PATH "models/sponza_multi_blas_transparent/Sponza.gltf"

#elif ASSET == 2
#define ASSET_PATH "models/cornellBox/CornellBox.gltf"

#elif ASSET == 3
#define ASSET_PATH "models/checkerboard/CheckerBoard.gltf"

#elif ASSET == 4	
#define ASSET_PATH "models/bistroexterior/BistroExterior.gltf"

#elif ASSET == 5	
#define ASSET_PATH "models/sponza_dynamic_multi_blas/Sponza.gltf"
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