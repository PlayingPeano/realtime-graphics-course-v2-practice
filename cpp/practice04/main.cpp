#include <wgpu_app.hpp>
#include <file_utils.hpp>
#include <obj_loader.hpp>

#include <webgpu.h>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <unordered_set>

static std::filesystem::path const projectRoot = PROJECT_ROOT;

static_assert(sizeof(ObjVertex) == 32);
static_assert(offsetof(ObjVertex, position) == 0);
static_assert(offsetof(ObjVertex, normal) == 12);

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
    pipelineLayoutDescriptor.immediateSize = 128;

    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDescriptor);

    WGPUVertexAttribute vertexAttributes[2] = {
        WGPU_VERTEX_ATTRIBUTE_INIT,
        WGPU_VERTEX_ATTRIBUTE_INIT,
    };
    vertexAttributes[0].format = WGPUVertexFormat_Float32x3;
    vertexAttributes[0].offset = offsetof(ObjVertex, position);
    vertexAttributes[0].shaderLocation = 0;

    vertexAttributes[1].format = WGPUVertexFormat_Float32x3;
    vertexAttributes[1].offset = offsetof(ObjVertex, normal);
    vertexAttributes[1].shaderLocation = 1;

    WGPUVertexBufferLayout vertexBufferLayout = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
    vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
    vertexBufferLayout.arrayStride = sizeof(ObjVertex);
    vertexBufferLayout.attributeCount = 2;
    vertexBufferLayout.attributes = vertexAttributes;

    WGPUColorTargetState colorTargetState = WGPU_COLOR_TARGET_STATE_INIT;
    colorTargetState.format = surfaceFormat;
    colorTargetState.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = WGPU_FRAGMENT_STATE_INIT;
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = {"fragmentMain", WGPU_STRLEN};
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTargetState;

    WGPUDepthStencilState depthStencilState = WGPU_DEPTH_STENCIL_STATE_INIT;
    depthStencilState.format = WGPUTextureFormat_Depth24Plus;
    depthStencilState.depthWriteEnabled = WGPUOptionalBool_True;
    depthStencilState.depthCompare = WGPUCompareFunction_Less;

    WGPURenderPipelineDescriptor renderPipelineDescriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    renderPipelineDescriptor.layout = pipelineLayout;
    renderPipelineDescriptor.vertex.module = shaderModule;
    renderPipelineDescriptor.vertex.entryPoint = {"vertexMain", WGPU_STRLEN};
    renderPipelineDescriptor.vertex.bufferCount = 1;
    renderPipelineDescriptor.vertex.buffers = &vertexBufferLayout;
    renderPipelineDescriptor.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    renderPipelineDescriptor.primitive.cullMode = WGPUCullMode_Back;
    renderPipelineDescriptor.fragment = &fragmentState;
    renderPipelineDescriptor.depthStencil = &depthStencilState;

    WGPURenderPipeline renderPipeline = wgpuDeviceCreateRenderPipeline(device, &renderPipelineDescriptor);
    wgpuPipelineLayoutRelease(pipelineLayout);

    return renderPipeline;
}

void destroyDepthBuffer(WGPUTexture & depthBuffer, WGPUTextureView & depthBufferView) {
    if (depthBufferView) {
        wgpuTextureViewRelease(depthBufferView);
        depthBufferView = nullptr;
    }
    if (depthBuffer) {
        wgpuTextureRelease(depthBuffer);
        depthBuffer = nullptr;
    }
}

void createDepthBuffer(WGPUDevice device, int width, int height,
                       WGPUTexture & depthBuffer, WGPUTextureView & depthBufferView) {
    destroyDepthBuffer(depthBuffer, depthBufferView);
    if (width <= 0 || height <= 0)
        return;

    WGPUTextureDescriptor textureDescriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    textureDescriptor.usage = WGPUTextureUsage_RenderAttachment;
    textureDescriptor.dimension = WGPUTextureDimension_2D;
    textureDescriptor.size = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    textureDescriptor.format = WGPUTextureFormat_Depth24Plus;
    textureDescriptor.mipLevelCount = 1;
    textureDescriptor.sampleCount = 1;
    depthBuffer = wgpuDeviceCreateTexture(device, &textureDescriptor);

    WGPUTextureViewDescriptor viewDescriptor = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    viewDescriptor.format = WGPUTextureFormat_Depth24Plus;
    viewDescriptor.dimension = WGPUTextureViewDimension_2D;
    viewDescriptor.aspect = WGPUTextureAspect_DepthOnly;
    viewDescriptor.baseMipLevel = 0;
    viewDescriptor.mipLevelCount = 1;
    viewDescriptor.baseArrayLayer = 0;
    viewDescriptor.arrayLayerCount = 1;
    viewDescriptor.usage = WGPUTextureUsage_RenderAttachment;
    depthBufferView = wgpuTextureCreateView(depthBuffer, &viewDescriptor);
}

math::matrix4f makeModelMatrix(float angle, float scale, float tx, float ty, float tz,
                               char plane) {
    float const c = std::cos(angle);
    float const s = std::sin(angle);

    if (plane == 'y') {
        return math::matrix4f{
            scale * c, 0.f, scale * s, tx,
            0.f, scale, 0.f, ty,
            -scale * s, 0.f, scale * c, tz,
            0.f, 0.f, 0.f, 1.f,
        };
    }
    if (plane == 'z') {
        return math::matrix4f{
            scale * c, -scale * s, 0.f, tx,
            scale * s, scale * c, 0.f, ty,
            0.f, 0.f, scale, tz,
            0.f, 0.f, 0.f, 1.f,
        };
    }
    return math::matrix4f{
        scale, 0.f, 0.f, tx,
        0.f, scale * c, -scale * s, ty,
        0.f, scale * s, scale * c, tz,
        0.f, 0.f, 0.f, 1.f,
    };
}

void drawBunny(WGPURenderPassEncoder renderPass, WGPUBuffer vertexBuffer, WGPUBuffer indexBuffer,
               std::uint32_t indexCount, math::matrix4f const & model,
               math::matrix4f const & viewProjection) {
    auto modelTranspose = math::transpose(model);
    auto viewProjectionTranspose = math::transpose(viewProjection);
    wgpuRenderPassEncoderSetImmediates(renderPass, 0, &modelTranspose, sizeof(modelTranspose));
    wgpuRenderPassEncoderSetImmediates(renderPass, sizeof(modelTranspose), &viewProjectionTranspose,
                                       sizeof(viewProjectionTranspose));
    wgpuRenderPassEncoderSetVertexBuffer(renderPass, 0, vertexBuffer, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetIndexBuffer(renderPass, indexBuffer, WGPUIndexFormat_Uint32, 0,
                                        WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDrawIndexed(renderPass, indexCount, 1, 0, 0, 0);
}

int main() try {
    WgpuApp app("Practice04", 1280, 720, true);

    WGPUShaderModule shaderModule = createShaderModule(app.device(), projectRoot / "shader.wgsl");
    WGPURenderPipeline renderPipeline = createPipeline(app.device(), shaderModule, app.surfaceFormat());

    ObjMesh bunny = loadObj(projectRoot / "bunny.obj");

    WGPUBufferDescriptor vertexBufferDescriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
    vertexBufferDescriptor.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    vertexBufferDescriptor.size = bunny.vertices.size() * sizeof(ObjVertex);
    WGPUBuffer vertexBuffer = wgpuDeviceCreateBuffer(app.device(), &vertexBufferDescriptor);
    wgpuQueueWriteBuffer(app.queue(), vertexBuffer, 0, bunny.vertices.data(),
                         vertexBufferDescriptor.size);

    WGPUBufferDescriptor indexBufferDescriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
    indexBufferDescriptor.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
    indexBufferDescriptor.size = bunny.indices.size() * sizeof(std::uint32_t);
    WGPUBuffer indexBuffer = wgpuDeviceCreateBuffer(app.device(), &indexBufferDescriptor);
    wgpuQueueWriteBuffer(app.queue(), indexBuffer, 0, bunny.indices.data(),
                         indexBufferDescriptor.size);

    WGPUTexture depthBuffer = nullptr;
    WGPUTextureView depthBufferView = nullptr;
    createDepthBuffer(app.device(), app.width(), app.height(), depthBuffer, depthBufferView);

    auto lastFrameStart = std::chrono::high_resolution_clock::now();
    float time = 0.f;
    float bunnyX = 0.f;
    float bunnyY = 0.f;
    float const moveSpeed = 1.0f;

    std::unordered_set<SDL_Keycode> keydown;

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
                createDepthBuffer(app.device(), app.width(), app.height(), depthBuffer, depthBufferView);
                break;
            case SDL_EVENT_KEY_DOWN:
                keydown.insert(event.key.key);
                break;
            case SDL_EVENT_KEY_UP:
                keydown.erase(event.key.key);
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

        if (keydown.contains(SDLK_LEFT))
            bunnyX -= moveSpeed * dt;
        if (keydown.contains(SDLK_RIGHT))
            bunnyX += moveSpeed * dt;
        if (keydown.contains(SDLK_DOWN))
            bunnyY -= moveSpeed * dt;
        if (keydown.contains(SDLK_UP))
            bunnyY += moveSpeed * dt;

        float const angle = time;
        float const scale = 0.5f;

        float const cameraZ = 3.f;
        math::matrix4f const view{
            1.f, 0.f, 0.f, 0.f,
            0.f, 1.f, 0.f, 0.f,
            0.f, 0.f, 1.f, -cameraZ,
            0.f, 0.f, 0.f, 1.f,
        };

        float const nearPlane = 0.1f;
        float const farPlane = 100.f;
        float const right = nearPlane;
        float const top = right * float(app.height()) / float(app.width());
        math::matrix4f const projection{
            nearPlane / right, 0.f, 0.f, 0.f,
            0.f, nearPlane / top, 0.f, 0.f,
            0.f, 0.f, farPlane / (nearPlane - farPlane), nearPlane * farPlane / (nearPlane - farPlane),
            0.f, 0.f, -1.f, 0.f,
        };

        math::matrix4f const viewProjection = projection * view;

        WGPUTextureView targetView = wgpuTextureCreateView(surfaceTexture->texture, nullptr);

        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(app.device(), nullptr);

        WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAttachment.view = targetView;
        colorAttachment.loadOp = WGPULoadOp_Clear;
        colorAttachment.storeOp = WGPUStoreOp_Store;
        colorAttachment.clearValue = {0.01, 0.02, 0.03, 1.0};

        WGPURenderPassDepthStencilAttachment depthAttachment =
            WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        depthAttachment.view = depthBufferView;
        depthAttachment.depthLoadOp = WGPULoadOp_Clear;
        depthAttachment.depthStoreOp = WGPUStoreOp_Discard;
        depthAttachment.depthClearValue = 1.0f;
        depthAttachment.depthReadOnly = false;

        WGPURenderPassDescriptor renderPassDescriptor = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        renderPassDescriptor.colorAttachmentCount = 1;
        renderPassDescriptor.colorAttachments = &colorAttachment;
        renderPassDescriptor.depthStencilAttachment = &depthAttachment;
        WGPURenderPassEncoder renderPass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDescriptor);

        wgpuRenderPassEncoderSetPipeline(renderPass, renderPipeline);

        auto const indexCount = static_cast<std::uint32_t>(bunny.indices.size());

        math::matrix4f const modelXY =
            makeModelMatrix(angle, scale, bunnyX - 1.2f, bunnyY, 0.f, 'z');
        math::matrix4f const modelXZ =
            makeModelMatrix(angle, scale, bunnyX, bunnyY, 0.f, 'y');
        math::matrix4f const modelYZ =
            makeModelMatrix(angle, scale, bunnyX + 1.2f, bunnyY, 0.f, 'x');

        drawBunny(renderPass, vertexBuffer, indexBuffer, indexCount, modelXY, viewProjection);
        drawBunny(renderPass, vertexBuffer, indexBuffer, indexCount, modelXZ, viewProjection);
        drawBunny(renderPass, vertexBuffer, indexBuffer, indexCount, modelYZ, viewProjection);

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

    destroyDepthBuffer(depthBuffer, depthBufferView);
    wgpuBufferRelease(indexBuffer);
    wgpuBufferRelease(vertexBuffer);
    wgpuRenderPipelineRelease(renderPipeline);
    wgpuShaderModuleRelease(shaderModule);
} catch (const std::exception &e) {
    std::cerr << "error: " << e.what() << std::endl;
    return EXIT_FAILURE;
}
