//
//  VROGeometrySubstrateMetal.cpp
//  ViroRenderer
//
//  Created by Raj Advani on 11/18/15.
//  Copyright © 2015 Viro Media. All rights reserved.
//
//  Permission is hereby granted, free of charge, to any person obtaining
//  a copy of this software and associated documentation files (the
//  "Software"), to deal in the Software without restriction, including
//  without limitation the rights to use, copy, modify, merge, publish,
//  distribute, sublicense, and/or sell copies of the Software, and to
//  permit persons to whom the Software is furnished to do so, subject to
//  the following conditions:
//
//  The above copyright notice and this permission notice shall be included
//  in all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
//  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
//  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
//  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
//  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
//  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
//  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "VROGeometrySubstrateMetal.h"
#if VRO_METAL

#include "VROImageUtil.h"
#include "VROGeometry.h"
#include "VROGeometrySource.h"
#include "VROGeometryElement.h"
#include "VROGeometryUtil.h"
#include "VRODriverMetal.h"
#include "VROMaterialSubstrateMetal.h"
#include "VROMaterial.h"
#include "VROLog.h"
#include "VROSharedStructures.h"
#include "VROMetalUtils.h"
#include "VROConcurrentBuffer.h"
#include <map>

VROGeometrySubstrateMetal::VROGeometrySubstrateMetal(const VROGeometry &geometry,
                                                     VRODriverMetal &driver) {
    id <MTLDevice> device = driver.getDevice();

    readGeometryElements(device, geometry.getGeometryElements());
    readGeometrySources(device, geometry.getGeometrySources());
    updatePipelineStates(geometry, driver);
    
    _viewUniformsBuffer = new VROConcurrentBuffer(sizeof(VROViewUniforms), @"VROViewUniformBuffer", device);
}

VROGeometrySubstrateMetal::~VROGeometrySubstrateMetal() {
    delete (_viewUniformsBuffer);
}

void VROGeometrySubstrateMetal::readGeometryElements(id <MTLDevice> device,
                                                     const std::vector<std::shared_ptr<VROGeometryElement>> &elements) {
    
    for (std::shared_ptr<VROGeometryElement> element : elements) {
        VROGeometryElementMetal elementMetal;
        
        int indexCount = VROGeometryUtilGetIndicesCount(element->getPrimitiveCount(),
                                                        element->getPrimitiveType());
        
        elementMetal.buffer = [device newBufferWithBytes:element->getData()->getData()
                                                  length:indexCount * element->getBytesPerIndex()
                                                 options:0];
        elementMetal.primitiveType = parsePrimitiveType(element->getPrimitiveType());
        elementMetal.indexCount = indexCount;
        elementMetal.indexType = (element->getBytesPerIndex() == 2) ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32;
        elementMetal.indexBufferOffset = 0;
        
        _elements.push_back(elementMetal);
    }
}

void VROGeometrySubstrateMetal::readGeometrySources(id <MTLDevice> device,
                                                    const std::vector<std::shared_ptr<VROGeometrySource>> &sources) {

    _vertexDescriptor = [MTLVertexDescriptor vertexDescriptor];

    // Partition sources by geometry element index.
    // Each GLTF primitive becomes one element; sources from different primitives
    // share buffer views but have different accessor offsets and vertex counts.
    // We build one interleaved MTL buffer per element so that render(elementIndex)
    // can bind _vars[elementIndex] at atIndex:0 with no uniform-slot conflicts.
    std::map<int, std::vector<std::shared_ptr<VROGeometrySource>>> byElement;
    for (const std::shared_ptr<VROGeometrySource> &src : sources) {
        byElement[src->getGeometryElementIndex()].push_back(src);
    }

    bool descriptorBuilt = false;

    for (auto &kv : byElement) {
        const std::vector<std::shared_ptr<VROGeometrySource>> &elemSrcs = kv.second;
        int vertexCount = elemSrcs.front()->getVertexCount();

        // Assign each source its own contiguous slot in the interleaved stride.
        // Each source is copied independently so that sources sharing a VROData
        // but at different byte offsets (packed-sequential non-interleaved GLTF)
        // are handled correctly.
        std::vector<int> srcOffset;
        int totalStride = 0;
        for (const std::shared_ptr<VROGeometrySource> &src : elemSrcs) {
            srcOffset.push_back(totalStride);
            totalStride += src->getDataStride();
        }

        std::vector<uint8_t> buf(vertexCount * totalStride, 0);
        for (int si = 0; si < (int)elemSrcs.size(); si++) {
            const std::shared_ptr<VROGeometrySource> &src = elemSrcs[si];
            std::shared_ptr<VROData> gData = src->getData();
            if (!gData || src->getDataStride() == 0) continue;
            int gStride     = src->getDataStride();
            int gDataOffset = src->getDataOffset();
            const uint8_t *pSrc = (const uint8_t *)gData->getData() + gDataOffset;
            for (int v = 0; v < vertexCount; v++) {
                memcpy(buf.data() + v * totalStride + srcOffset[si],
                       pSrc + v * gStride, gStride);
            }
        }

        VROVertexArrayMetal var;
        var.buffer = [device newBufferWithBytes:buf.data() length:buf.size() options:0];
        var.buffer.label = @"VROGeometryVertexBuffer";
        _vars.push_back(var);

        // Build the shared vertex descriptor from the first element.
        // All elements are assumed to have the same attribute layout.
        if (!descriptorBuilt) {
            _vertexDescriptor.layouts[0].stepRate     = 1;
            _vertexDescriptor.layouts[0].stride       = totalStride;
            _vertexDescriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

            for (int si = 0; si < (int)elemSrcs.size(); si++) {
                std::shared_ptr<VROGeometrySource> src = elemSrcs[si];
                int attrIdx = VROGeometryUtilParseAttributeIndex(src->getSemantic());
                _vertexDescriptor.attributes[attrIdx].format      = parseVertexFormat(src);
                _vertexDescriptor.attributes[attrIdx].offset      = srcOffset[si];
                _vertexDescriptor.attributes[attrIdx].bufferIndex = 0;
            }
            descriptorBuilt = true;
        }
    }
}

void VROGeometrySubstrateMetal::updatePipelineStates(const VROGeometry &geometry,
                                                     VRODriverMetal &driver) {
    
    id <MTLDevice> device = driver.getDevice();
    const std::vector<std::shared_ptr<VROMaterial>> &materials = geometry.getMaterials();
    
    for (int i = 0; i < _elements.size(); i++) {
        VROGeometryElementMetal element = _elements[i];
        const std::shared_ptr<VROMaterial> &material = materials[i % materials.size()];
        
        id <MTLRenderPipelineState> pipelineState = createRenderPipelineState(material, driver);
        _elementPipelineStates.push_back(pipelineState);
        
        id <MTLDepthStencilState> depthStencilState = createDepthStencilState(material, device);
        _elementDepthStates.push_back(depthStencilState);
    }
}

id <MTLRenderPipelineState> VROGeometrySubstrateMetal::createRenderPipelineState(const std::shared_ptr<VROMaterial> &material,
                                                                                 VRODriverMetal &driver) {
    
    id <MTLDevice> device = driver.getDevice();
    // Non-owning shared_ptr: driver is managed externally (e.g. by VROViewMetal or
    // the CompositorServices render loop). The null deleter prevents double-free.
    std::shared_ptr<VRODriver> driverPtr(static_cast<VRODriver *>(&driver), [](VRODriver *) {});
    VROMaterialSubstrateMetal *substrate = static_cast<VROMaterialSubstrateMetal *>(material->getSubstrate(driverPtr));

    MTLRenderPipelineDescriptor *pipelineStateDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
    pipelineStateDescriptor.label = @"VROLayerPipeline";
    pipelineStateDescriptor.sampleCount = driver.getSampleCount();
    pipelineStateDescriptor.vertexFunction = substrate->getVertexProgram();
    pipelineStateDescriptor.fragmentFunction = substrate->getFragmentProgram();
    pipelineStateDescriptor.vertexDescriptor = _vertexDescriptor;
    pipelineStateDescriptor.colorAttachments[0].pixelFormat = driver.getColorPixelFormat();
    pipelineStateDescriptor.colorAttachments[0].blendingEnabled = YES;
    pipelineStateDescriptor.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
    pipelineStateDescriptor.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
    pipelineStateDescriptor.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    pipelineStateDescriptor.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
    pipelineStateDescriptor.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    pipelineStateDescriptor.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    pipelineStateDescriptor.depthAttachmentPixelFormat = driver.getDepthPixelFormat();
    pipelineStateDescriptor.stencilAttachmentPixelFormat = driver.getStencilPixelFormat();
    
    NSError *error = NULL;
    id <MTLRenderPipelineState> pipelineState = [device newRenderPipelineStateWithDescriptor:pipelineStateDescriptor
                                                                                       error:&error];
    if (!pipelineState) {
        NSLog(@"Failed to created pipeline state, error %@", error);
    }
    return  pipelineState;
}

id <MTLDepthStencilState> VROGeometrySubstrateMetal::createDepthStencilState(const std::shared_ptr<VROMaterial> &material,
                                                                             id <MTLDevice> device) {
    
    MTLDepthStencilDescriptor *depthStateDesc = [[MTLDepthStencilDescriptor alloc] init];
    depthStateDesc.depthWriteEnabled = material->getWritesToDepthBuffer();
    
    if (material->getReadsFromDepthBuffer()) {
        /*
         Using LessEqual ensures that outgoing material transitions work correctly,
         in that we can render the same face twice (once outgoing, once incoming), and
         the incoming will not fail the depth test despite having the same depth as
         the outgoing.
         */
        depthStateDesc.depthCompareFunction = MTLCompareFunctionLessEqual;
    }
    else {
        depthStateDesc.depthCompareFunction = MTLCompareFunctionAlways;
    }
    
    return [device newDepthStencilStateWithDescriptor:depthStateDesc];
}

MTLVertexFormat VROGeometrySubstrateMetal::parseVertexFormat(std::shared_ptr<VROGeometrySource> &source) {
    // Currently assuming floats
    switch (source->getBytesPerComponent()) {
        case 2:
            switch (source->getComponentsPerVertex()) {
                case 1:
                    return MTLVertexFormatFloat;
                    
                case 2:
                    return MTLVertexFormatFloat2;
                    
                case 3:
                    return MTLVertexFormatFloat3;
                    
                case 4:
                    return MTLVertexFormatFloat4;
                    
                default:
                    pabort();
                    return MTLVertexFormatFloat;
            }
            
        case 4:
            switch (source->getComponentsPerVertex()) {
                case 1:
                    return MTLVertexFormatFloat;
                    
                case 2:
                    return MTLVertexFormatFloat2;
                    
                case 3:
                    return MTLVertexFormatFloat3;
                    
                case 4:
                    return MTLVertexFormatFloat4;
                    
                default:
                    pabort();
                    return MTLVertexFormatFloat;
            }
            
        default:
            pabort();
            return MTLVertexFormatFloat;
    }
}

MTLPrimitiveType VROGeometrySubstrateMetal::parsePrimitiveType(VROGeometryPrimitiveType primitive) {
    switch (primitive) {
        case VROGeometryPrimitiveType::Triangle:
            return MTLPrimitiveTypeTriangle;
            
        case VROGeometryPrimitiveType::TriangleStrip:
            return MTLPrimitiveTypeTriangleStrip;
            
        case VROGeometryPrimitiveType::Line:
            return MTLPrimitiveTypeLine;
            
        case VROGeometryPrimitiveType::Point:
            return MTLPrimitiveTypePoint;
            
        default:
            break;
    }
}

void VROGeometrySubstrateMetal::render(const VROGeometry &geometry,
                                       int elementIndex,
                                       VROMatrix4f transform,
                                       VROMatrix4f normalMatrix,
                                       float opacity,
                                       const std::shared_ptr<VROMaterial> &material,
                                       const VRORenderContext &context,
                                       std::shared_ptr<VRODriver> &driver) {
    
    VRODriverMetal &metal = (VRODriverMetal &)(*driver);
    id <MTLRenderCommandEncoder> renderEncoder = metal.getActiveEncoder();
    
    int frame = context.getFrame();
    VROEyeType eyeType = context.getEyeType();
    
    VROMatrix4f viewMatrix = context.getViewMatrix();
    VROMatrix4f projectionMatrix = context.getProjectionMatrix();
    
    if (geometry.isCameraEnclosure()) {
        viewMatrix = context.getEnclosureViewMatrix();
    }
    
    [renderEncoder pushDebugGroup:@"VROGeometry"];
    VROGeometryElementMetal element = _elements[elementIndex];
    
    /*
     Configure the view uniforms.
     */
    VROMatrix4f modelview = viewMatrix.multiply(transform);
    VROViewUniforms *viewUniforms = (VROViewUniforms *)_viewUniformsBuffer->getWritableContents(eyeType, frame);
    
    viewUniforms->normal_matrix = toMatrixFloat4x4(normalMatrix);
    viewUniforms->model_matrix = toMatrixFloat4x4(transform);
    viewUniforms->modelview_matrix = toMatrixFloat4x4(modelview);
    viewUniforms->modelview_projection_matrix = toMatrixFloat4x4(projectionMatrix.multiply(modelview));
    viewUniforms->view_matrix = toMatrixFloat4x4(viewMatrix);
    viewUniforms->projection_matrix = toMatrixFloat4x4(projectionMatrix);
    viewUniforms->camera_position = toVectorFloat3(context.getCamera().getPosition());
    
    [renderEncoder setVertexBuffer:_viewUniformsBuffer->getMTLBuffer(eyeType)
                            offset:_viewUniformsBuffer->getWriteOffset(frame) atIndex:1];
    
    /*
     Determine if the material has been updated. If so, we need to update our pipeline and
     depth states.
     */
    if (material->isUpdated()) {
        _elementPipelineStates[elementIndex] = createRenderPipelineState(material, metal);
        _elementDepthStates[elementIndex] = createDepthStencilState(material, metal.getDevice());
    }
    
    VROMaterialSubstrateMetal *substrate = static_cast<VROMaterialSubstrateMetal *>(material->getSubstrate(driver));
    id <MTLRenderPipelineState> pipelineState = _elementPipelineStates[elementIndex];
    id <MTLDepthStencilState> depthState = _elementDepthStates[elementIndex];
    
    if (elementIndex < (int)_vars.size()) {
        [renderEncoder setVertexBuffer:_vars[elementIndex].buffer offset:0 atIndex:0];
    }
    
    /*
     Note that outgoing materials share the same pipeline state as their counterparts. This is because
     they always have the same shaders and vertex layouts.
     */
    renderMaterial(substrate, element, pipelineState, depthState, renderEncoder, opacity,
                   context, driver);
    
    [renderEncoder popDebugGroup];
}

void VROGeometrySubstrateMetal::renderMaterial(VROMaterialSubstrateMetal *material,
                                               VROGeometryElementMetal &element,
                                               id <MTLRenderPipelineState> pipelineState,
                                               id <MTLDepthStencilState> depthStencilState,
                                               id <MTLRenderCommandEncoder> renderEncoder,
                                               float opacity,
                                               const VRORenderContext &renderContext,
                                               std::shared_ptr<VRODriver> &driver) {
    
    int frame = renderContext.getFrame();
    VROEyeType eyeType = renderContext.getEyeType();
    
    [renderEncoder setRenderPipelineState:pipelineState];
    [renderEncoder setDepthStencilState:depthStencilState];
    
    VROConcurrentBuffer &materialBuffer = material->bindMaterialUniforms(opacity, eyeType, frame);
    [renderEncoder setVertexBuffer:materialBuffer.getMTLBuffer(eyeType)
                            offset:materialBuffer.getWriteOffset(frame)
                           atIndex:2];
    [renderEncoder setFragmentBuffer:materialBuffer.getMTLBuffer(eyeType)
                              offset:materialBuffer.getWriteOffset(frame)
                             atIndex:2];
    
    // Bind custom uniforms at index 3
    VROConcurrentBuffer *customBuffer = material->getCustomUniformsBuffer();
    if (customBuffer) {
        [renderEncoder setVertexBuffer:customBuffer->getMTLBuffer(eyeType)
                                offset:customBuffer->getWriteOffset(frame)
                               atIndex:3];
        [renderEncoder setFragmentBuffer:customBuffer->getMTLBuffer(eyeType)
                                  offset:customBuffer->getWriteOffset(frame)
                                 atIndex:3];
    }
    
    const std::vector<std::shared_ptr<VROTexture>> &textures = material->getTextures();
    for (int j = 0; j < textures.size(); ++j) {
        VROTextureSubstrateMetal *substrate = (VROTextureSubstrateMetal *) textures[j]->getSubstrate(0, driver, true);
        if (!substrate) {
            // Use a blank placeholder if a texture is not yet available (i.e.
            // during video texture loading)
            std::shared_ptr<VROTexture> blank = getBlankTexture(VROTextureType::Texture2D);
            substrate = (VROTextureSubstrateMetal *) blank->getSubstrate(0, driver, true);
        }
        
        [renderEncoder setFragmentTexture:substrate->getTexture() atIndex:j];
    }
    
    [renderEncoder drawIndexedPrimitives:element.primitiveType
                              indexCount:element.indexCount
                               indexType:element.indexType
                             indexBuffer:element.buffer
                       indexBufferOffset:0];
}

#endif
