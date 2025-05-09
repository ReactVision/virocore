cmake_minimum_required(VERSION 3.4.1)

# Due to some changes, we need to increase the recursion depth to 1024:
# https://github.com/jeremyong/Selene/issues/167#issuecomment-267751122
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ftemplate-depth=1024")
endif()

set(VIRO_ANDROID_INCLUDES ${CMAKE_SOURCE_DIR}/../libs)
set(VIRO_ANDROID_LIBS     ${CMAKE_SOURCE_DIR}/../sharedCode/src/main/jniLibs)
set(VIRO_RENDERER_SRC     ${CMAKE_SOURCE_DIR}/../../ViroRenderer)
set(VIRO_CAPI_SRC         ${CMAKE_SOURCE_DIR}/../../ViroRenderer/capi)
set(VIRO_ANDROID_SRC      ${CMAKE_SOURCE_DIR}/../sharedCode/src/main/cpp)

include_directories(${VIRO_RENDERER_SRC}
                    ${VIRO_CAPI_SRC}
                    ${VIRO_ANDROID_SRC}
                    ${VIRO_ANDROID_SRC}/jni
                    ${VIRO_ANDROID_INCLUDES}/gvr/include
                    ${VIRO_ANDROID_INCLUDES}/ovr/include
                    ${VIRO_ANDROID_INCLUDES}/freetype/include
                    ${VIRO_ANDROID_INCLUDES}/protobuf/include
                    ${VIRO_ANDROID_INCLUDES}/bullet/include
                    ${VIRO_ANDROID_INCLUDES}/opencv/include
                    ${VIRO_ANDROID_INCLUDES}/arcore/include)

# Creates and names a library, sets it as either STATIC
# or SHARED, and provides the relative paths to its source code.
# You can define multiple libraries, and CMake builds it for you.
# Gradle automatically packages shared libraries with your APK.

add_library( # Sets the name of the library.
             viro_renderer

             # Sets the library as a shared library.
             SHARED

             # Provides a relative path to your source file(s).
             # Associated headers in the same location as their source
             # file are automatically included.

             # Android Platform
             ${VIRO_ANDROID_SRC}/debug_stacktrace.cpp
             ${VIRO_ANDROID_SRC}/debug_mapinfo.cpp
             ${VIRO_ANDROID_SRC}/VROLooper.cpp
             ${VIRO_ANDROID_SRC}/VROSceneRenderer.cpp
             ${VIRO_ANDROID_SRC}/VROSceneRendererGVR.cpp
             ${VIRO_ANDROID_SRC}/VROSceneRendererOVR.cpp
             ${VIRO_ANDROID_SRC}/VROSceneRendererSceneView.cpp
             ${VIRO_ANDROID_SRC}/VROSample.cpp
             ${VIRO_ANDROID_SRC}/VROImageAndroid.cpp
             ${VIRO_ANDROID_SRC}/VROAudioPlayerAndroid.cpp
             ${VIRO_ANDROID_SRC}/VROAVPlayer.cpp
             ${VIRO_ANDROID_SRC}/VROVideoTextureAVP.cpp
             ${VIRO_ANDROID_SRC}/VROTypefaceAndroid.cpp
             ${VIRO_ANDROID_SRC}/VROInputControllerDaydream.cpp
             ${VIRO_ANDROID_SRC}/VROInputControllerCardboard.cpp
             ${VIRO_ANDROID_SRC}/VROInputControllerOVR.cpp
             ${VIRO_ANDROID_SRC}/VROAVRecorderAndroid.cpp
             ${VIRO_ANDROID_SRC}/VROTextureReader.cpp
             ${VIRO_ANDROID_SRC}/VROAndroidViewTexture.cpp
             ${VIRO_ANDROID_SRC}/VRODriverOpenGLAndroid.cpp

             # C API
             ${VIRO_CAPI_SRC}/Node_JNI.cpp
             ${VIRO_CAPI_SRC}/Geometry_JNI.cpp
             ${VIRO_CAPI_SRC}/Submesh_JNI.cpp
             ${VIRO_CAPI_SRC}/SceneController_JNI.cpp
             ${VIRO_CAPI_SRC}/Camera_JNI.cpp
             ${VIRO_CAPI_SRC}/Box_JNI.cpp
             ${VIRO_CAPI_SRC}/Image_JNI.cpp
             ${VIRO_CAPI_SRC}/Controller_JNI.cpp
             ${VIRO_CAPI_SRC}/Surface_JNI.cpp
             ${VIRO_CAPI_SRC}/Polygon_JNI.cpp
             ${VIRO_CAPI_SRC}/Sphere_JNI.cpp
             ${VIRO_CAPI_SRC}/Material_JNI.cpp
             ${VIRO_CAPI_SRC}/Texture_JNI.cpp
             ${VIRO_CAPI_SRC}/VideoTexture_JNI.cpp
             ${VIRO_CAPI_SRC}/AnimatedTexture_JNI.cpp
             ${VIRO_CAPI_SRC}/VideoDelegate_JNI.cpp
             ${VIRO_CAPI_SRC}/ViroContext_JNI.cpp
             ${VIRO_CAPI_SRC}/EventDelegate_JNI.cpp
             ${VIRO_CAPI_SRC}/AnimationChain_JNI.cpp
             ${VIRO_CAPI_SRC}/AnimationGroup_JNI.cpp
             ${VIRO_CAPI_SRC}/ExecutableAnimation_JNI.cpp
             ${VIRO_CAPI_SRC}/LazyMaterial_JNI.cpp
             ${VIRO_CAPI_SRC}/Light_JNI.cpp
             ${VIRO_CAPI_SRC}/AmbientLight_JNI.cpp
             ${VIRO_CAPI_SRC}/SpotLight_JNI.cpp
             ${VIRO_CAPI_SRC}/DirectionalLight_JNI.cpp
             ${VIRO_CAPI_SRC}/OmniLight_JNI.cpp
             ${VIRO_CAPI_SRC}/Quad_JNI.cpp
             ${VIRO_CAPI_SRC}/Sound_JNI.cpp
             ${VIRO_CAPI_SRC}/SoundField_JNI.cpp
             ${VIRO_CAPI_SRC}/SpatialSound_JNI.cpp
             ${VIRO_CAPI_SRC}/SoundDelegate_JNI.cpp
             ${VIRO_CAPI_SRC}/Text_JNI.cpp
             ${VIRO_CAPI_SRC}/TextDelegate_JNI.cpp
             ${VIRO_CAPI_SRC}/SoundData_JNI.cpp
             ${VIRO_CAPI_SRC}/Object_JNI.cpp
             ${VIRO_CAPI_SRC}/OBJLoaderDelegate_JNI.cpp
             ${VIRO_CAPI_SRC}/Polyline_JNI.cpp
             ${VIRO_CAPI_SRC}/PhysicsDelegate_JNI.cpp
             ${VIRO_CAPI_SRC}/TransformDelegate_JNI.cpp
             ${VIRO_CAPI_SRC}/FixedParticleEmitter_JNI.cpp
             ${VIRO_CAPI_SRC}/ParticleEmitter_JNI.cpp
             ${VIRO_CAPI_SRC}/Portal_JNI.cpp
             ${VIRO_CAPI_SRC}/PortalScene_JNI.cpp
             ${VIRO_CAPI_SRC}/PortalDelegate_JNI.cpp
             ${VIRO_CAPI_SRC}/PhysicsBody_JNI.cpp
             ${VIRO_CAPI_SRC}/AnimationTransaction_JNI.cpp
             ${VIRO_CAPI_SRC}/ViroUtils_JNI.cpp

             # Math
             ${VIRO_RENDERER_SRC}/VROLineSegment.cpp
             ${VIRO_RENDERER_SRC}/VROTriangle.cpp
             ${VIRO_RENDERER_SRC}/VROQuaternion.cpp
             ${VIRO_RENDERER_SRC}/VROPlane.cpp
             ${VIRO_RENDERER_SRC}/VROFrustum.cpp
             ${VIRO_RENDERER_SRC}/VROFrustumPlane.cpp
             ${VIRO_RENDERER_SRC}/VROBoundingBox.cpp
             ${VIRO_RENDERER_SRC}/VROFrustumBoxIntersectionMetadata.cpp
             ${VIRO_RENDERER_SRC}/VROVector3f.cpp
             ${VIRO_RENDERER_SRC}/VROVector4f.cpp
             ${VIRO_RENDERER_SRC}/VROMatrix4f.cpp
             ${VIRO_RENDERER_SRC}/VROMath.cpp
             ${VIRO_RENDERER_SRC}/VROProjector.cpp
             ${VIRO_RENDERER_SRC}/VRODualQuaternion.cpp

             # Util
             ${VIRO_RENDERER_SRC}/VROAllocationTracker.cpp
             ${VIRO_RENDERER_SRC}/VROTime.cpp
             ${VIRO_RENDERER_SRC}/VROLog.cpp
             ${VIRO_RENDERER_SRC}/VROByteBuffer.cpp
             ${VIRO_RENDERER_SRC}/VROImageUtil.cpp
             ${VIRO_RENDERER_SRC}/VROData.cpp
             ${VIRO_RENDERER_SRC}/VROGeometryUtil.cpp
             ${VIRO_RENDERER_SRC}/VROTextureUtil.cpp
             ${VIRO_RENDERER_SRC}/VROStringUtil.cpp
             ${VIRO_RENDERER_SRC}/VROPlatformUtil.cpp
             ${VIRO_RENDERER_SRC}/VROFBXLoader.cpp
             ${VIRO_RENDERER_SRC}/VROGLTFLoader.cpp
             ${VIRO_RENDERER_SRC}/VROModelIOUtil.cpp
             ${VIRO_RENDERER_SRC}/VROOBJLoader.cpp
             ${VIRO_RENDERER_SRC}/VROHDRLoader.cpp
             ${VIRO_RENDERER_SRC}/VROAnimatedTextureOpenGL.cpp
             ${VIRO_RENDERER_SRC}/tiny_obj_loader.cc
             ${VIRO_RENDERER_SRC}/VROGVRUtil.cpp
             ${VIRO_RENDERER_SRC}/VROThreadRestricted.cpp
             ${VIRO_RENDERER_SRC}/VROCompress.cpp
             ${VIRO_RENDERER_SRC}/VRORenderUtil.cpp
             ${VIRO_RENDERER_SRC}/VROTaskQueue.cpp
             ${VIRO_RENDERER_SRC}/VROSparseBitSet.cpp
             ${VIRO_RENDERER_SRC}/Nodes.pb.cc
             ${VIRO_RENDERER_SRC}/gzip_stream.cpp

             # Renderer
             ${VIRO_RENDERER_SRC}/VRODriver.cpp
             ${VIRO_RENDERER_SRC}/VRODriverOpenGL.cpp
             ${VIRO_RENDERER_SRC}/VROScene.cpp
             ${VIRO_RENDERER_SRC}/VROSceneController.cpp
             ${VIRO_RENDERER_SRC}/VROCamera.cpp
             ${VIRO_RENDERER_SRC}/VRONode.cpp
             ${VIRO_RENDERER_SRC}/VROPortal.cpp
             ${VIRO_RENDERER_SRC}/VROPortalFrame.cpp
             ${VIRO_RENDERER_SRC}/VROGeometry.cpp
             ${VIRO_RENDERER_SRC}/VROGeometryElement.cpp
             ${VIRO_RENDERER_SRC}/VROGeometrySource.cpp
             ${VIRO_RENDERER_SRC}/VROMaterial.cpp
             ${VIRO_RENDERER_SRC}/VROMaterialVisual.cpp
             ${VIRO_RENDERER_SRC}/VROTexture.cpp
             ${VIRO_RENDERER_SRC}/VROLight.cpp
             ${VIRO_RENDERER_SRC}/VROBillboardConstraint.cpp
             ${VIRO_RENDERER_SRC}/VROBoneConstraint.cpp
             ${VIRO_RENDERER_SRC}/VROTransformConstraint.cpp
             ${VIRO_RENDERER_SRC}/VRORenderer.cpp
             ${VIRO_RENDERER_SRC}/VROFrameSynchronizerInternal.cpp
             ${VIRO_RENDERER_SRC}/VROPortalTraversalListener.cpp
             ${VIRO_RENDERER_SRC}/VROGeometrySubstrate.cpp
             ${VIRO_RENDERER_SRC}/VROTextureSubstrate.cpp
             ${VIRO_RENDERER_SRC}/VROMaterialSubstrate.cpp
             ${VIRO_RENDERER_SRC}/VRONodeCamera.cpp
             ${VIRO_RENDERER_SRC}/VROInputControllerBase.cpp
             ${VIRO_RENDERER_SRC}/VROFrameScheduler.cpp
             ${VIRO_RENDERER_SRC}/VROChoreographer.cpp
             ${VIRO_RENDERER_SRC}/VROPortalTreeRenderPass.cpp
             ${VIRO_RENDERER_SRC}/VRORenderTargetOpenGL.cpp
             ${VIRO_RENDERER_SRC}/VROImageShaderProgram.cpp
             ${VIRO_RENDERER_SRC}/VROImagePostProcessOpenGL.cpp
             ${VIRO_RENDERER_SRC}/VROParticleEmitter.cpp
             ${VIRO_RENDERER_SRC}/VROParticleUBO.cpp
             ${VIRO_RENDERER_SRC}/VROShadowPreprocess.cpp
             ${VIRO_RENDERER_SRC}/VROShadowMapRenderPass.cpp
             ${VIRO_RENDERER_SRC}/VROShaderFactory.cpp
             ${VIRO_RENDERER_SRC}/VROShaderCapabilities.cpp
             ${VIRO_RENDERER_SRC}/VROTextureReference.cpp
             ${VIRO_RENDERER_SRC}/VROMaterialShaderBinding.cpp
             ${VIRO_RENDERER_SRC}/VROPostProcessEffectFactory.cpp
             ${VIRO_RENDERER_SRC}/VROToneMappingRenderPass.cpp
             ${VIRO_RENDERER_SRC}/VROGaussianBlurRenderPass.cpp
             ${VIRO_RENDERER_SRC}/VROIBLPreprocess.cpp
             ${VIRO_RENDERER_SRC}/VROEquirectangularToCubeRenderPass.cpp
             ${VIRO_RENDERER_SRC}/VROIrradianceRenderPass.cpp
             ${VIRO_RENDERER_SRC}/VROPrefilterRenderPass.cpp
             ${VIRO_RENDERER_SRC}/VROBRDFRenderPass.cpp
             ${VIRO_RENDERER_SRC}/VROFixedParticleEmitter.cpp
             ${VIRO_RENDERER_SRC}/VROVertexBufferOpenGL.cpp

             # AR (no arcore dependencies)
             ${VIRO_RENDERER_SRC}/VROARSceneController.cpp
             ${VIRO_RENDERER_SRC}/VROARScene.cpp
             ${VIRO_RENDERER_SRC}/VROARNode.cpp
             ${VIRO_RENDERER_SRC}/VROARConstraintMatcher.cpp
             ${VIRO_RENDERER_SRC}/VROARDeclarativeSession.cpp
             ${VIRO_RENDERER_SRC}/VROARImperativeSession.cpp
             ${VIRO_RENDERER_SRC}/VROInputControllerAR.cpp
             ${VIRO_RENDERER_SRC}/VROARShadow.cpp
             ${VIRO_ANDROID_SRC}/VROInputControllerARAndroid.cpp
             ${VIRO_ANDROID_SRC}/VROSceneRendererARCore.cpp
             ${VIRO_ANDROID_SRC}/arcore/VROARSessionARCore.cpp
             ${VIRO_ANDROID_SRC}/arcore/VROARFrameARCore.cpp
             ${VIRO_ANDROID_SRC}/arcore/VROARCameraARCore.cpp
             ${VIRO_ANDROID_SRC}/arcore/VROARAnchorARCore.cpp
             ${VIRO_ANDROID_SRC}/arcore/VROARHitTestResultARCore.cpp
             ${VIRO_ANDROID_SRC}/arcore/VROYuvImageConverter.cpp
             ${VIRO_ANDROID_SRC}/arcore/VROCloudAnchorProviderARCore.cpp
             ${VIRO_ANDROID_SRC}/arcore/ARUtils_JNI.cpp
             ${VIRO_ANDROID_SRC}/arcore/ARNode_JNI.cpp
             ${VIRO_ANDROID_SRC}/arcore/ARDeclarativeNode_JNI.cpp
             ${VIRO_ANDROID_SRC}/arcore/ARDeclarativePlane_JNI.cpp
             ${VIRO_ANDROID_SRC}/arcore/ARSceneController_JNI.cpp
             ${VIRO_ANDROID_SRC}/arcore/ARImageTarget_JNI.cpp
             ${VIRO_ANDROID_SRC}/arcore/ARDeclarativeImageNode_JNI.cpp
             ${VIRO_ANDROID_SRC}/arcore/ARHitTestResult_JNI.cpp

             # AR (Image Tracking)
             ../../ViroRenderer/VROARImageTargetAndroid.cpp

             # Animation
             ${VIRO_RENDERER_SRC}/VROTimingFunction.cpp
             ${VIRO_RENDERER_SRC}/VROTransaction.cpp
             ${VIRO_RENDERER_SRC}/VROAnimatable.cpp
             ${VIRO_RENDERER_SRC}/VROAction.cpp
             ${VIRO_RENDERER_SRC}/VROAnimationChain.cpp
             ${VIRO_RENDERER_SRC}/VROAnimationGroup.cpp
             ${VIRO_RENDERER_SRC}/VROPropertyAnimation.cpp
             ${VIRO_RENDERER_SRC}/VROSkinner.cpp
             ${VIRO_RENDERER_SRC}/VROSkeleton.cpp
             ${VIRO_RENDERER_SRC}/VROBone.cpp
             ${VIRO_RENDERER_SRC}/VROIKRig.cpp
             ${VIRO_RENDERER_SRC}/VROBoneUBO.cpp
             ${VIRO_RENDERER_SRC}/VROBodyTrackerController.cpp
             ${VIRO_RENDERER_SRC}/VROBodyIKController.cpp
             ${VIRO_RENDERER_SRC}/VROSkeletalAnimation.cpp
             ${VIRO_RENDERER_SRC}/VROLayeredSkeletalAnimation.cpp
             ${VIRO_RENDERER_SRC}/VROKeyframeAnimation.cpp
             ${VIRO_RENDERER_SRC}/VROMorpher.cpp

             # Shapes
             ${VIRO_RENDERER_SRC}/VROBox.cpp
             ${VIRO_RENDERER_SRC}/VROSkybox.cpp
             ${VIRO_RENDERER_SRC}/VROSurface.cpp
             ${VIRO_RENDERER_SRC}/VROPolygon.cpp
             ${VIRO_RENDERER_SRC}/VROTorusKnot.cpp
             ${VIRO_RENDERER_SRC}/VROShapeUtils.cpp
             ${VIRO_RENDERER_SRC}/VROSphere.cpp
             ${VIRO_RENDERER_SRC}/VROPolyline.cpp
             ${VIRO_RENDERER_SRC}/VROVideoSurface.cpp
             ${VIRO_RENDERER_SRC}/VROPencil.cpp

             # UI
             ${VIRO_RENDERER_SRC}/VROReticle.cpp
             ${VIRO_RENDERER_SRC}/VROText.cpp
             ${VIRO_RENDERER_SRC}/VROTextFormatter.cpp
             ${VIRO_RENDERER_SRC}/VROTypeface.cpp
             ${VIRO_RENDERER_SRC}/VROTypefaceCollection.cpp
             ${VIRO_RENDERER_SRC}/VROFontUtil.cpp
             ${VIRO_RENDERER_SRC}/VROCharmapCoverage.cpp
             ${VIRO_RENDERER_SRC}/VROKnuthPlassFormatter.cpp
             ${VIRO_RENDERER_SRC}/VROVectorizer.cpp
             ${VIRO_RENDERER_SRC}/VROContour.cpp
             ${VIRO_RENDERER_SRC}/VRODebugHUD.cpp

             # OpenGL
             ${VIRO_RENDERER_SRC}/VROGeometrySubstrateOpenGL.cpp
             ${VIRO_RENDERER_SRC}/VROMaterialSubstrateOpenGL.cpp
             ${VIRO_RENDERER_SRC}/VROTextureSubstrateOpenGL.cpp
             ${VIRO_RENDERER_SRC}/VROUniform.cpp
             ${VIRO_RENDERER_SRC}/VROShaderProgram.cpp
             ${VIRO_RENDERER_SRC}/VROShaderModifier.cpp
             ${VIRO_RENDERER_SRC}/VROLightingUBO.cpp
             ${VIRO_RENDERER_SRC}/VROGlyphOpenGL.cpp
             ${VIRO_RENDERER_SRC}/VROGlyphAtlasOpenGL.cpp

             # Poly2Tri
             ${VIRO_RENDERER_SRC}/poly2tri/sweep/advancing_front.cc
             ${VIRO_RENDERER_SRC}/poly2tri/sweep/cdt.cc
             ${VIRO_RENDERER_SRC}/poly2tri/sweep/sweep_context.cc
             ${VIRO_RENDERER_SRC}/poly2tri/sweep/sweep.cc
             ${VIRO_RENDERER_SRC}/poly2tri/common/shapes.cc

             # GifLib
             ${VIRO_RENDERER_SRC}/giflib/openbsd-reallocarray.c
             ${VIRO_RENDERER_SRC}/giflib/gifalloc.c
             ${VIRO_RENDERER_SRC}/giflib/dgif_lib.c
             ${VIRO_RENDERER_SRC}/giflib/gif_hash.c

             # GVR Audio
             ${VIRO_RENDERER_SRC}/VROSoundGVR.cpp
             ${VIRO_RENDERER_SRC}/VROSoundDataGVR.cpp

             # Physics
             ${VIRO_RENDERER_SRC}/VROPhysicsWorld.cpp
             ${VIRO_RENDERER_SRC}/VROPhysicsBody.cpp
             ${VIRO_RENDERER_SRC}/VROPhysicsShape.cpp

             # Android JNI
             ${VIRO_ANDROID_SRC}/jni/VRORenderer_JNI.cpp
             ${VIRO_ANDROID_SRC}/jni/MediaRecorder_JNI.cpp
             ${VIRO_ANDROID_SRC}/jni/VRORendererARCore_JNI.cpp
             ${VIRO_ANDROID_SRC}/jni/VROCameraImageListener.cpp
             ${VIRO_ANDROID_SRC}/jni/ARImageDatabaseLoaderDelegate.cpp

             # Renderer Test
             ${VIRO_RENDERER_SRC}/VRORendererTestHarness.cpp
             ${VIRO_RENDERER_SRC}/VROTestUtil.cpp
             ${VIRO_RENDERER_SRC}/VROPhysicsTest.cpp
             ${VIRO_RENDERER_SRC}/VROARDraggableNodeTest.cpp
             ${VIRO_RENDERER_SRC}/VROARPlaneTest.cpp
             ${VIRO_RENDERER_SRC}/VROShadowTest.cpp
             ${VIRO_RENDERER_SRC}/VROARShadowTest.cpp
             ${VIRO_RENDERER_SRC}/VROBloomTest.cpp
             ${VIRO_RENDERER_SRC}/VROParticleTest.cpp
             ${VIRO_RENDERER_SRC}/VROFBXTest.cpp
             ${VIRO_RENDERER_SRC}/VROGLTFTest.cpp
             ${VIRO_RENDERER_SRC}/VROIKTest.cpp
             ${VIRO_RENDERER_SRC}/VROHDRTest.cpp
             ${VIRO_RENDERER_SRC}/VRONormalMapTest.cpp
             ${VIRO_RENDERER_SRC}/VROOBJTest.cpp
             ${VIRO_RENDERER_SRC}/VROPerfTest.cpp
             ${VIRO_RENDERER_SRC}/VROPortalTest.cpp
             ${VIRO_RENDERER_SRC}/VROShadowTest.cpp
             ${VIRO_RENDERER_SRC}/VROStereoscopicTest.cpp
             ${VIRO_RENDERER_SRC}/VROTextTest.cpp
             ${VIRO_RENDERER_SRC}/VROTorusTest.cpp
             ${VIRO_RENDERER_SRC}/VROVideoSphereTest.cpp
             ${VIRO_RENDERER_SRC}/VROBoxTest.cpp
             ${VIRO_RENDERER_SRC}/VROPolylineTest.cpp
             ${VIRO_RENDERER_SRC}/VROPBRDirectTest.cpp
             ${VIRO_RENDERER_SRC}/VROPBRTexturedTest.cpp
             ${VIRO_RENDERER_SRC}/VRODiffuseIrradianceTest.cpp
             ${VIRO_RENDERER_SRC}/VROPhotometricLightTest.cpp
             ${VIRO_RENDERER_SRC}/VRORendererSettingsTest.cpp
             ${VIRO_RENDERER_SRC}/VROToneMappingTest.cpp
             ${VIRO_RENDERER_SRC}/VROPolygonTest.cpp
             ${VIRO_RENDERER_SRC}/VROBodyTrackerTest.cpp
             ${VIRO_RENDERER_SRC}/VROBodyRecognitionTest.cpp
             ${VIRO_RENDERER_SRC}/VROObjectRecognitionTest.cpp
             ${VIRO_RENDERER_SRC}/VROBodyMesherTest.cpp
             )

# Add pre-built libraries
add_library( lib-gvr
             SHARED
             IMPORTED )
set_target_properties( lib-gvr
                       PROPERTIES IMPORTED_LOCATION
                       ${VIRO_ANDROID_LIBS}/${ANDROID_ABI}/libgvr.so )

add_library( lib-gvr-audio
             SHARED
             IMPORTED )
set_target_properties( lib-gvr-audio
                       PROPERTIES IMPORTED_LOCATION
                       ${VIRO_ANDROID_LIBS}/${ANDROID_ABI}/libgvr_audio.so )

add_library( lib-ovr
             SHARED
             IMPORTED )
set_target_properties( lib-ovr
                       PROPERTIES IMPORTED_LOCATION
                       ${VIRO_ANDROID_LIBS}/${ANDROID_ABI}/libvrapi.so )

add_library( lib-freetype
             SHARED
             IMPORTED )
set_target_properties( lib-freetype
                       PROPERTIES IMPORTED_LOCATION
                       ${VIRO_ANDROID_LIBS}/${ANDROID_ABI}/libfreetyped.so )

add_library( lib-protobuf
             STATIC
             IMPORTED )
set_target_properties( lib-protobuf
                       PROPERTIES IMPORTED_LOCATION
                       ${VIRO_ANDROID_LIBS}/${ANDROID_ABI}/libprotobuf-lite.a )

add_library( lib-BulletCollision
             SHARED
             IMPORTED )
set_target_properties( lib-BulletCollision
                       PROPERTIES IMPORTED_LOCATION
                       ${VIRO_ANDROID_LIBS}/${ANDROID_ABI}/libBulletCollision.so )

add_library( lib-BulletDynamics
             SHARED
             IMPORTED )
set_target_properties( lib-BulletDynamics
                       PROPERTIES IMPORTED_LOCATION
                       ${VIRO_ANDROID_LIBS}/${ANDROID_ABI}/libBulletDynamics.so )

add_library( lib-BulletSoftBody
             SHARED
             IMPORTED )
set_target_properties( lib-BulletSoftBody
                       PROPERTIES IMPORTED_LOCATION
                       ${VIRO_ANDROID_LIBS}/${ANDROID_ABI}/libBulletSoftBody.so )

add_library( lib-LinearMath
             SHARED
             IMPORTED )
set_target_properties( lib-LinearMath
                       PROPERTIES IMPORTED_LOCATION
                       ${VIRO_ANDROID_LIBS}/${ANDROID_ABI}/libLinearMath.so )

# Searches for a specified prebuilt library and stores the path as a
# variable. Because system libraries are included in the search path by
# default, you only need to specify the name of the public NDK library
# you want to add. CMake verifies that the library exists before
# completing its build. The first argument to find_library is the output
# variable where the path to the library will be stored.

find_library( lib-log log )
find_library( lib-android android )
find_library( lib-jnigraphics jnigraphics )
find_library( lib-egl EGL )
find_library( lib-GLESv3 GLESv3 )
find_library( lib-z z )

include(AndroidNdkModules)
android_ndk_import_module_cpufeatures()

# Specifies libraries CMake should link to your target library. You
# can link multiple libraries, such as libraries you define in the
# build script, prebuilt third-party libraries, or system libraries.

target_link_libraries( # Specifies the target library.
                       viro_renderer

                       # Libraries linked with target
                       ${lib-log}
                       ${lib-android}
                       ${lib-jnigraphics}
                       ${lib-egl}
                       ${lib-GLESv3}

                       atomic
                       lib-gvr
                       lib-gvr-audio
                       lib-ovr
                       lib-freetype
                       lib-protobuf
                       lib-BulletCollision
                       lib-BulletDynamics
                       lib-BulletSoftBody
                       lib-LinearMath

                       cpufeatures
                       ${lib-z}
        )
