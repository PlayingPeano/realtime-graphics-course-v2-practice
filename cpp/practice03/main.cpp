#include <wgpu_app.hpp>
#include <file_utils.hpp>
#include <math/aliases.hpp>
#include <math/detail/alloca.hpp>

#include <webgpu.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <span>
#include <vector>

static std::filesystem::path const projectRoot = PROJECT_ROOT;

struct vertex {
    math::vector2f position;
    math::vector4ub color;
    float distance = 0.f;
};

static_assert(sizeof(vertex) == 16);
static_assert(offsetof(vertex, position) == 0);
static_assert(offsetof(vertex, color) == 8);
static_assert(offsetof(vertex, distance) == 12);

vertex lerp(vertex const & v0, vertex const & v1, float t) {
    return {
        .position = math::lerp(v0.position, v1.position, t),
        .color = math::cast<std::uint8_t>(math::lerp(math::cast<float>(v0.color), math::cast<float>(v1.color), t)),
        .distance = math::lerp(v0.distance, v1.distance, t),
    };
}

vertex in_place_bezier(std::span<vertex> vertices, float t) {
    std::size_t const n = vertices.size();

    for (std::size_t k = n - 1; k > 0; --k) {
        for (std::size_t i = 0; i < k; ++i) {
            vertices[i] = lerp(vertices[i], vertices[i + 1], t);
        }
    }

    return vertices[0];
}

vertex bezier(std::span<vertex const> vertices, float t) {
    std::size_t const n = vertices.size();

    vertex *scratch = math_alloca(vertex, n);
    std::copy(vertices.begin(), vertices.end(), scratch);
    return in_place_bezier(std::span{scratch, n}, t);
}

WGPUShaderModule createShaderModule(WGPUDevice device, std::filesystem::path const &path) {
    auto const source = loadFile(path);

    WGPUShaderSourceWGSL shaderSourceWGSL = WGPU_SHADER_SOURCE_WGSL_INIT;
    shaderSourceWGSL.code = {source.data(), source.size()};

    WGPUShaderModuleDescriptor shaderModuleDescriptor = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    shaderModuleDescriptor.nextInChain = &shaderSourceWGSL.chain;

    return wgpuDeviceCreateShaderModule(device, &shaderModuleDescriptor);
}

WGPURenderPipeline createPipeline(WGPUDevice device, WGPUShaderModule shaderModule,
                                  WGPUTextureFormat surfaceFormat) {
    WGPUPipelineLayoutDescriptor pipelineLayoutDescriptor = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    pipelineLayoutDescriptor.immediateSize = 80;

    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDescriptor);

    WGPUVertexAttribute vertexAttributes[3] = {
        WGPU_VERTEX_ATTRIBUTE_INIT,
        WGPU_VERTEX_ATTRIBUTE_INIT,
        WGPU_VERTEX_ATTRIBUTE_INIT,
    };
    vertexAttributes[0].format = WGPUVertexFormat_Float32x2;
    vertexAttributes[0].offset = offsetof(vertex, position);
    vertexAttributes[0].shaderLocation = 0;

    vertexAttributes[1].format = WGPUVertexFormat_Unorm8x4;
    vertexAttributes[1].offset = offsetof(vertex, color);
    vertexAttributes[1].shaderLocation = 1;

    vertexAttributes[2].format = WGPUVertexFormat_Float32;
    vertexAttributes[2].offset = offsetof(vertex, distance);
    vertexAttributes[2].shaderLocation = 2;

    WGPUVertexBufferLayout vertexBufferLayout = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
    vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
    vertexBufferLayout.arrayStride = sizeof(vertex);
    vertexBufferLayout.attributeCount = 3;
    vertexBufferLayout.attributes = vertexAttributes;

    WGPUColorTargetState colorTargetState = WGPU_COLOR_TARGET_STATE_INIT;
    colorTargetState.format = surfaceFormat;
    colorTargetState.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = WGPU_FRAGMENT_STATE_INIT;
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = {"fragmentMain", WGPU_STRLEN};
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTargetState;

    WGPURenderPipelineDescriptor renderPipelineDescriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    renderPipelineDescriptor.layout = pipelineLayout;
    renderPipelineDescriptor.vertex.module = shaderModule;
    renderPipelineDescriptor.vertex.entryPoint = {"vertexMain", WGPU_STRLEN};
    renderPipelineDescriptor.vertex.bufferCount = 1;
    renderPipelineDescriptor.vertex.buffers = &vertexBufferLayout;
    renderPipelineDescriptor.primitive.topology = WGPUPrimitiveTopology_LineStrip;
    renderPipelineDescriptor.fragment = &fragmentState;

    WGPURenderPipeline renderPipeline = wgpuDeviceCreateRenderPipeline(device, &renderPipelineDescriptor);
    wgpuPipelineLayoutRelease(pipelineLayout);

    return renderPipeline;
}

WGPUBuffer createVertexBuffer(WGPUDevice device, std::uint64_t byteSize) {
    WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
    descriptor.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex;
    descriptor.size = byteSize;
    return wgpuDeviceCreateBuffer(device, &descriptor);
}

struct GpuCurve {
    WGPUBuffer buffer = nullptr;
    std::size_t capacityBytes = 0;
    std::size_t vertexCount = 0;
};

void destroyGpuCurve(GpuCurve & curve) {
    if (curve.buffer) {
        wgpuBufferRelease(curve.buffer);
        curve.buffer = nullptr;
    }
    curve.capacityBytes = 0;
    curve.vertexCount = 0;
}

void uploadCurve(WGPUDevice device, WGPUQueue queue, GpuCurve & curve, std::vector<vertex> const & verts) {
    curve.vertexCount = verts.size();
    if (verts.empty()) {
        return;
    }

    std::uint64_t const bytes = verts.size() * sizeof(vertex);
    if (!curve.buffer || curve.capacityBytes < bytes) {
        if (curve.buffer)
            wgpuBufferRelease(curve.buffer);
        std::uint64_t newCapacity = std::max<std::uint64_t>(bytes, 256);
        if (curve.capacityBytes > 0)
            newCapacity = std::max(newCapacity, static_cast<std::uint64_t>(curve.capacityBytes) * 2);
        curve.buffer = createVertexBuffer(device, newCapacity);
        curve.capacityBytes = newCapacity;
    }

    wgpuQueueWriteBuffer(queue, curve.buffer, 0, verts.data(), bytes);
}

void rebuildBezier(std::vector<vertex> const & control, int quality, std::vector<vertex> & out) {
    out.clear();
    if (control.size() < 2 || quality < 1)
        return;

    std::size_t const segmentCount = (control.size() - 1) * static_cast<std::size_t>(quality);
    std::size_t const pointCount = segmentCount + 1;
    out.reserve(pointCount);

    math::vector4ub const curveColor{255, 220, 70, 255};
    float distance = 0.f;

    for (std::size_t i = 0; i < pointCount; ++i) {
        float const t = static_cast<float>(i) / static_cast<float>(segmentCount);
        vertex v = bezier(control, t);
        v.color = curveColor;

        if (i == 0) {
            v.distance = 0.f;
        } else {
            distance += math::length(v.position - out.back().position);
            v.distance = distance;
        }
        out.push_back(v);
    }
}

int main() try {
    WgpuApp app("Practice03", 1280, 720, false);

    WGPUShaderModule shaderModule = createShaderModule(app.device(), projectRoot / "shader.wgsl");
    WGPURenderPipeline renderPipeline = createPipeline(app.device(), shaderModule, app.surfaceFormat());

    auto lastFrameStart = std::chrono::high_resolution_clock::now();
    float time = 0.f;

    std::vector<vertex> vertices;
    std::vector<vertex> bezierVertices;
    int quality = 4;
    bool polylineDirty = true;
    bool bezierDirty = true;

    GpuCurve polylineGpu;
    GpuCurve bezierGpu;

    math::vector2f mouse{0.f, 0.f};

    static math::vector4ub const palette[] = {
        {125, 207, 182, 255},
        {251, 209, 162, 255},
        {247, 146, 86, 255},
        {160, 140, 255, 255},
        {255, 120, 180, 255},
    };

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                app.resize(event.window.data1, event.window.data2);
                break;
            case SDL_EVENT_KEY_DOWN:
                if (event.key.key == SDLK_LEFT) {
                    // Нажата клавиша влево
                    if (quality > 1) {
                        --quality;
                        bezierDirty = true;
                    }
                }
                if (event.key.key == SDLK_RIGHT) {
                    // Нажата клавиша вправо
                    ++quality;
                    bezierDirty = true;
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                mouse = math::vector2f{event.motion.x, event.motion.y} * app.pixelDensity();
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                mouse = math::vector2f{event.button.x, event.button.y} * app.pixelDensity();
                if (event.button.button == SDL_BUTTON_LEFT) {
                    // Нажата левая кнопка
                    math::vector4ub const color = palette[vertices.size() % (sizeof(palette) / sizeof(palette[0]))];
                    vertices.push_back({mouse, color, 0.f});
                    polylineDirty = true;
                    bezierDirty = true;
                }
                if (event.button.button == SDL_BUTTON_RIGHT) {
                    // Нажата правая кнопка
                    if (!vertices.empty()) {
                        vertices.pop_back();
                        polylineDirty = true;
                        bezierDirty = true;
                    }
                }
                break;
            }
        }

        std::optional<WGPUSurfaceTexture> surfaceTexture = app.beginFrame();
        if (!surfaceTexture) {
            continue;
        }

        auto const now = std::chrono::high_resolution_clock::now();
        float const dt = std::chrono::duration<float>(now - lastFrameStart).count();
        time += dt;
        lastFrameStart = now;

        if (bezierDirty) {
            rebuildBezier(vertices, quality, bezierVertices);
        }

        if (polylineDirty) {
            uploadCurve(app.device(), app.queue(), polylineGpu, vertices);
            polylineDirty = false;
        }
        if (bezierDirty) {
            uploadCurve(app.device(), app.queue(), bezierGpu, bezierVertices);
            bezierDirty = false;
        }

        float const width = float(app.width());
        float const height = float(app.height());
        float const viewMatrix[16] = {
            2.f / width, 0.f, 0.f, 0.f,
            0.f, -2.f / height, 0.f, 0.f,
            0.f, 0.f, 1.f, 0.f,
            -1.f, 1.f, 0.f, 1.f,
        };

        struct ImmediatesData {
            float view[16];
            float time;
            float dash;
        };
        ImmediatesData immediates{};
        std::copy(std::begin(viewMatrix), std::end(viewMatrix), immediates.view);
        immediates.time = time;

        WGPUTextureView targetView = wgpuTextureCreateView(surfaceTexture->texture, nullptr);

        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(app.device(), nullptr);

        WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAttachment.view = targetView;
        colorAttachment.loadOp = WGPULoadOp_Clear;
        colorAttachment.storeOp = WGPUStoreOp_Store;
        colorAttachment.clearValue = {0.07, 0.21, 0.30, 1.0};

        WGPURenderPassDescriptor renderPassDescriptor = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        renderPassDescriptor.colorAttachmentCount = 1;
        renderPassDescriptor.colorAttachments = &colorAttachment;
        WGPURenderPassEncoder renderPass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDescriptor);

        wgpuRenderPassEncoderSetPipeline(renderPass, renderPipeline);

        if (polylineGpu.vertexCount >= 2 && polylineGpu.buffer) {
            immediates.dash = 0.f;
            wgpuRenderPassEncoderSetImmediates(renderPass, 0, &immediates, sizeof(immediates));
            wgpuRenderPassEncoderSetVertexBuffer(renderPass, 0, polylineGpu.buffer, 0,
                                                 polylineGpu.vertexCount * sizeof(vertex));
            wgpuRenderPassEncoderDraw(renderPass, static_cast<uint32_t>(polylineGpu.vertexCount), 1, 0, 0);
        }

        if (bezierGpu.vertexCount >= 2 && bezierGpu.buffer) {
            immediates.dash = 1.f;
            wgpuRenderPassEncoderSetImmediates(renderPass, 0, &immediates, sizeof(immediates));
            wgpuRenderPassEncoderSetVertexBuffer(renderPass, 0, bezierGpu.buffer, 0,
                                                 bezierGpu.vertexCount * sizeof(vertex));
            wgpuRenderPassEncoderDraw(renderPass, static_cast<uint32_t>(bezierGpu.vertexCount), 1, 0, 0);
        }

        wgpuRenderPassEncoderEnd(renderPass);
        wgpuRenderPassEncoderRelease(renderPass);

        WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(encoder, nullptr);
        wgpuCommandEncoderRelease(encoder);

        wgpuQueueSubmit(app.queue(), 1, &commandBuffer);
        wgpuCommandBufferRelease(commandBuffer);

        wgpuSurfacePresent(app.surface());

        wgpuTextureViewRelease(targetView);
        wgpuTextureRelease(surfaceTexture->texture);
    }

    destroyGpuCurve(polylineGpu);
    destroyGpuCurve(bezierGpu);
    wgpuRenderPipelineRelease(renderPipeline);
    wgpuShaderModuleRelease(shaderModule);
} catch (const std::exception &e) {
    std::cerr << "error: " << e.what() << std::endl;
    return EXIT_FAILURE;
}
