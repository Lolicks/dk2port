import Foundation
import Metal
import MetalKit

/// Centralised depth configuration for every DK2 render pass and pipeline.
///
/// Metal requires the pipeline's `depthStencilPixelFormat` to agree with the
/// attachment declared by the render pass. Keeping both in one place avoids
/// the (silent, hard to debug) mismatch between the tile, mesh, particle and
/// fog pipelines.
enum DK2DepthSupport {
    static let pixelFormat: MTLPixelFormat = .depth32float

    /// Must be called before the view creates its first depth texture.
    static func configureView(_ view: MTKView) {
        view.depthStencilPixelFormat = pixelFormat
        view.sampleCount = 1
    }

    /// Call on `view.currentRenderPassDescriptor` before creating the encoder.
    static func configureRenderPass(_ descriptor: MTLRenderPassDescriptor) {
        guard let depth = descriptor.depthAttachment else { return }
        depth.loadAction = .clear
        depth.clearDepth = 1.0
        depth.storeAction = .dontCare
    }

    @discardableResult
    static func configurePipeline(
        _ descriptor: MTLRenderPipelineDescriptor,
        colorFormat: MTLPixelFormat
    ) -> MTLRenderPipelineDescriptor {
        descriptor.colorAttachments[0].pixelFormat = colorFormat
        descriptor.depthStencilPixelFormat = pixelFormat
        return descriptor
    }

    static func depthState(
        device: MTLDevice,
        write: Bool,
        compare: MTLCompareFunction = .less
    ) -> MTLDepthStencilState? {
        let descriptor = MTLDepthStencilDescriptor()
        descriptor.depthCompareFunction = compare
        descriptor.isDepthWriteEnabled = write
        return device.makeDepthStencilState(descriptor: descriptor)
    }
}
