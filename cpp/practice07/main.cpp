#include <wgpu_app.hpp>
#include <file_utils.hpp>
#include <image_utils.hpp>
#include <obj_loader.hpp>
#include <math/math.hpp>

#include <webgpu.h>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <iostream>
#include <numbers>
#include <unordered_set>

static std::filesystem::path const projectRoot = PROJECT_ROOT;

struct CameraUniforms
{
    math::matrix4f viewProjectionTranspose;
    math::vector3f position;
    float padding = 0.f;
};

struct LightsUniforms
{
    math::vector3f ambientIntensity;
    float pad0 = 0.f;
    math::vector3f directionalDirection;
    float pad1 = 0.f;
    math::vector3f directionalIntensity;
    float pad2 = 0.f;
    math::vector3f pointPosition;
    float pad3 = 0.f;
    math::vector3f pointIntensity;
    float pad4 = 0.f;
    math::vector3f pointAttenuation;
    float pad5 = 0.f;
};

static_assert(sizeof(LightsUniforms) == 96);
static_assert(offsetof(LightsUniforms, directionalDirection) == 16);
static_assert(offsetof(LightsUniforms, pointPosition) == 48);
static_assert(offsetof(LightsUniforms, pointAttenuation) == 80);

struct Immediates
{
    math::matrix4f modelTranspose;
    math::vector3f albedo;
    float roughness;
    float specularFactor;
    float pad[3]{};
};

static_assert(sizeof(Immediates) == 96);
static_assert(offsetof(Immediates, albedo) == 64);
static_assert(offsetof(Immediates, roughness) == 76);
static_assert(offsetof(Immediates, specularFactor) == 80);

struct LightImmediates
{
    math::vector3f position;
    float padding = 0.f;
};

static_assert(sizeof(LightImmediates) == 16);

WGPUShaderModule createShaderModule(WGPUDevice device, std::filesystem::path const & path) {
    auto const source = loadFile(path);

    WGPUShaderSourceWGSL shaderSourceWGSL = WGPU_SHADER_SOURCE_WGSL_INIT;
    shaderSourceWGSL.code = {source.data(), source.size()};

    WGPUShaderModuleDescriptor shaderModuleDescriptor = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    shaderModuleDescriptor.nextInChain = &shaderSourceWGSL.chain;

    return wgpuDeviceCreateShaderModule(device, &shaderModuleDescriptor);
}

WGPUBuffer createBuffer(WGPUDevice device, std::size_t size, WGPUBufferUsage usage) {
    WGPUBufferDescriptor bufferDescriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
    bufferDescriptor.size = size;
    bufferDescriptor.usage = usage | WGPUBufferUsage_CopyDst;
    return wgpuDeviceCreateBuffer(device, &bufferDescriptor);
}

template <typename T>
WGPUBuffer loadBuffer(WGPUDevice device, WGPUQueue queue, std::vector<T> const & data, WGPUBufferUsage usage) {
    auto buffer = createBuffer(device, data.size() * sizeof(T), usage);
    wgpuQueueWriteBuffer(queue, buffer, 0, data.data(), data.size() * sizeof(T));
    return buffer;
}

WGPUBindGroupLayout createUniformBufferBindGroupLayout(WGPUDevice device) {
    WGPUBindGroupLayoutEntry layoutEntries[1] = {WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT};
    layoutEntries[0].binding = 0;
    layoutEntries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    layoutEntries[0].buffer.type = WGPUBufferBindingType_Uniform;

    WGPUBindGroupLayoutDescriptor bindGroupLayoutDescriptor = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    bindGroupLayoutDescriptor.entries = layoutEntries;
    bindGroupLayoutDescriptor.entryCount = std::size(layoutEntries);

    return wgpuDeviceCreateBindGroupLayout(device, &bindGroupLayoutDescriptor);
}

WGPUBindGroup createUniformBufferBindGroup(WGPUDevice device, WGPUBindGroupLayout bindGroupLayout, WGPUBuffer uniformBuffer) {
    WGPUBindGroupEntry entries[1] = {WGPU_BIND_GROUP_ENTRY_INIT};
    entries[0].binding = 0;
    entries[0].buffer = uniformBuffer;
    entries[0].size = WGPU_WHOLE_SIZE;

    WGPUBindGroupDescriptor bindGroupDescriptor = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bindGroupDescriptor.layout = bindGroupLayout;
    bindGroupDescriptor.entries = entries;
    bindGroupDescriptor.entryCount = 1;

    return wgpuDeviceCreateBindGroup(device, &bindGroupDescriptor);
}

WGPURenderPipeline createPipeline(WGPUDevice device, WGPUShaderModule shaderModule, WGPUTextureFormat surfaceFormat,
        WGPUBindGroupLayout cameraBindGroupLayout, WGPUBindGroupLayout lightsBindGroupLayout) {
    WGPUBindGroupLayout bindGroupLayouts[2] = {
        cameraBindGroupLayout,
        lightsBindGroupLayout,
    };

    WGPUPipelineLayoutDescriptor pipelineLayoutDescriptor = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    pipelineLayoutDescriptor.immediateSize = sizeof(Immediates);
    pipelineLayoutDescriptor.bindGroupLayouts = bindGroupLayouts;
    pipelineLayoutDescriptor.bindGroupLayoutCount = std::size(bindGroupLayouts);

    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDescriptor);

    WGPUColorTargetState colorTargetState = WGPU_COLOR_TARGET_STATE_INIT;
    colorTargetState.format = surfaceFormat;
    colorTargetState.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = WGPU_FRAGMENT_STATE_INIT;
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = {"fragmentMain", WGPU_STRLEN};
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTargetState;

    WGPUVertexAttribute attributes[3] = {WGPU_VERTEX_ATTRIBUTE_INIT, WGPU_VERTEX_ATTRIBUTE_INIT};
    attributes[0].offset = 0;
    attributes[0].shaderLocation = 0;
    attributes[0].format = WGPUVertexFormat_Float32x3;
    attributes[1].offset = 12;
    attributes[1].shaderLocation = 1;
    attributes[1].format = WGPUVertexFormat_Float32x3;
    attributes[2].offset = 24;
    attributes[2].shaderLocation = 2;
    attributes[2].format = WGPUVertexFormat_Float32x2;

    WGPUVertexBufferLayout vertexBufferLayout = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
    vertexBufferLayout.arrayStride = sizeof(ObjVertex);
    vertexBufferLayout.attributeCount = std::size(attributes);
    vertexBufferLayout.attributes = attributes;
    vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;

    WGPUDepthStencilState depthStencilState = WGPU_DEPTH_STENCIL_STATE_INIT;
    depthStencilState.format = WGPUTextureFormat_Depth24Plus;
    depthStencilState.depthWriteEnabled = WGPUOptionalBool_True;
    depthStencilState.depthCompare = WGPUCompareFunction_Less;

    WGPURenderPipelineDescriptor renderPipelineDescriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    renderPipelineDescriptor.layout = pipelineLayout;
    renderPipelineDescriptor.vertex.module = shaderModule;
    renderPipelineDescriptor.vertex.entryPoint = {"vertexMain", WGPU_STRLEN};
    renderPipelineDescriptor.vertex.buffers = &vertexBufferLayout;
    renderPipelineDescriptor.vertex.bufferCount = 1;
    renderPipelineDescriptor.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    renderPipelineDescriptor.primitive.cullMode = WGPUCullMode_Back;
    renderPipelineDescriptor.fragment = &fragmentState;
    renderPipelineDescriptor.depthStencil = &depthStencilState;

    WGPURenderPipeline renderPipeline = wgpuDeviceCreateRenderPipeline(device, &renderPipelineDescriptor);
    wgpuPipelineLayoutRelease(pipelineLayout);

    return renderPipeline;
}

WGPURenderPipeline createLightPipeline(WGPUDevice device, WGPUShaderModule shaderModule, WGPUTextureFormat surfaceFormat,
        WGPUBindGroupLayout cameraBindGroupLayout) {
    WGPUPipelineLayoutDescriptor pipelineLayoutDescriptor = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    pipelineLayoutDescriptor.immediateSize = sizeof(LightImmediates);
    pipelineLayoutDescriptor.bindGroupLayouts = &cameraBindGroupLayout;
    pipelineLayoutDescriptor.bindGroupLayoutCount = 1;

    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDescriptor);

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
    renderPipelineDescriptor.primitive.topology = WGPUPrimitiveTopology_LineList;
    renderPipelineDescriptor.fragment = &fragmentState;
    renderPipelineDescriptor.depthStencil = &depthStencilState;

    WGPURenderPipeline renderPipeline = wgpuDeviceCreateRenderPipeline(device, &renderPipelineDescriptor);
    wgpuPipelineLayoutRelease(pipelineLayout);

    return renderPipeline;
}

int main() try {
    WgpuApp app("Practice07", 1280, 720, true);

    WGPUBindGroupLayout cameraBindGroupLayout = createUniformBufferBindGroupLayout(app.device());
    WGPUBindGroupLayout lightsBindGroupLayout = createUniformBufferBindGroupLayout(app.device());

    WGPUShaderModule shaderModule = createShaderModule(app.device(), projectRoot / "shader.wgsl");
    WGPUShaderModule lightShaderModule = createShaderModule(app.device(), projectRoot / "light_shader.wgsl");

    WGPURenderPipeline renderPipeline = createPipeline(app.device(), shaderModule, app.surfaceFormat(), cameraBindGroupLayout, lightsBindGroupLayout);
    WGPURenderPipeline lightRenderPipeline = createLightPipeline(app.device(), lightShaderModule, app.surfaceFormat(), cameraBindGroupLayout);

    WGPUBuffer cameraUniformBuffer = createBuffer(app.device(), sizeof(CameraUniforms), WGPUBufferUsage_Uniform);
    WGPUBuffer lightsUniformBuffer = createBuffer(app.device(), sizeof(LightsUniforms), WGPUBufferUsage_Uniform);
    WGPUBindGroup cameraBindGroup = createUniformBufferBindGroup(app.device(), cameraBindGroupLayout, cameraUniformBuffer);
    WGPUBindGroup lightsBindGroup = createUniformBufferBindGroup(app.device(), lightsBindGroupLayout, lightsUniformBuffer);

    ObjMesh suzanne = loadObj(projectRoot / "suzanne.obj");

    auto vertexBuffer = loadBuffer(app.device(), app.queue(), suzanne.vertices, WGPUBufferUsage_Vertex);
    auto indexBuffer = loadBuffer(app.device(), app.queue(), suzanne.indices, WGPUBufferUsage_Index);

    WGPUTexture depthBuffer = nullptr;
    WGPUTextureView depthBufferView = nullptr;

    auto lastFrameStart = std::chrono::high_resolution_clock::now();
    float time = 0.f;

    float camera_distance = 8.f;
    float camera_rotation = 0.f;

    bool paused = false;

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
                break;
            case SDL_EVENT_KEY_DOWN:
                keydown.insert(event.key.key);
                if (event.key.key == SDLK_SPACE)
                    paused ^= true;
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

        if (!depthBuffer || wgpuTextureGetWidth(depthBuffer) != app.width() || wgpuTextureGetHeight(depthBuffer) != app.height()) {
            if (depthBufferView) wgpuTextureViewRelease(depthBufferView);
            if (depthBuffer) wgpuTextureRelease(depthBuffer);

            WGPUTextureDescriptor depthBufferDescriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
            depthBufferDescriptor.usage = WGPUTextureUsage_RenderAttachment;
            depthBufferDescriptor.dimension = WGPUTextureDimension_2D;
            depthBufferDescriptor.size = {(std::uint32_t)app.width(), (std::uint32_t)app.height(), 1};
            depthBufferDescriptor.format = WGPUTextureFormat_Depth24Plus;
            depthBuffer = wgpuDeviceCreateTexture(app.device(), &depthBufferDescriptor);

            WGPUTextureViewDescriptor depthBufferViewDescriptor = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
            depthBufferViewDescriptor.format = WGPUTextureFormat_Depth24Plus;
            depthBufferViewDescriptor.dimension = WGPUTextureViewDimension_2D;
            depthBufferViewDescriptor.mipLevelCount = 1;
            depthBufferViewDescriptor.arrayLayerCount = 1;
            depthBufferViewDescriptor.aspect = WGPUTextureAspect_DepthOnly;
            depthBufferViewDescriptor.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TransientAttachment;
            depthBufferView = wgpuTextureCreateView(depthBuffer, &depthBufferViewDescriptor);
        }

        auto const now = std::chrono::high_resolution_clock::now();
        float const dt = std::chrono::duration<float>(now - lastFrameStart).count();
        if (!paused) time += dt;
        lastFrameStart = now;

        if (keydown.contains(SDLK_LEFT)) camera_rotation += 5.f * dt;
        if (keydown.contains(SDLK_RIGHT)) camera_rotation -= 5.f * dt;
        if (keydown.contains(SDLK_UP)) camera_distance += 7.f * dt;
        if (keydown.contains(SDLK_DOWN)) camera_distance -= 7.f * dt;

        math::matrix4f const view = math::translation(math::vector{0.f, 0.f, - camera_distance})
            * math::rotation_xz(camera_rotation);
        math::matrix4f const projection = math::perspective(std::numbers::pi_v<float> / 3.f, app.width() * 1.f / app.height(), 0.01f, 100.f);

        CameraUniforms const cameraUniforms {
            .viewProjectionTranspose = math::transpose(projection * view),
            .position = math::head<3>(math::inverse(view).value() * math::vector{0.f, 0.f, 0.f, 1.f}),
        };

        wgpuQueueWriteBuffer(app.queue(), cameraUniformBuffer, 0, &cameraUniforms, sizeof(cameraUniforms));

        math::vector3f const directionalDirection = math::normalized(math::vector{
            0.4f * std::sin(time * 0.5f),
            0.8f,
            0.7f,
        });
        math::vector3f const pointPosition{
            2.3f * std::sin(time * 0.7f),
            2.3f * std::cos(time * 0.7f),
            1.6f,
        };

        LightsUniforms const lightUniforms {
            .ambientIntensity = {0.02f, 0.02f, 0.025f},
            .directionalDirection = directionalDirection,
            .directionalIntensity = {0.9f, 0.85f, 0.75f},
            .pointPosition = pointPosition,
            .pointIntensity = {0.3f, 8.f, 0.4f},
            .pointAttenuation = {1.f, 0.f, 0.1f},
        };

        wgpuQueueWriteBuffer(app.queue(), lightsUniformBuffer, 0, &lightUniforms, sizeof(lightUniforms));

        WGPUTextureView targetView = wgpuTextureCreateView(surfaceTexture->texture, nullptr);

        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(app.device(), nullptr);

        WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAttachment.view = targetView;
        colorAttachment.loadOp = WGPULoadOp_Clear;
        colorAttachment.storeOp = WGPUStoreOp_Store;
        colorAttachment.clearValue = {0.01, 0.03, 0.05, 1.0};

        WGPURenderPassDepthStencilAttachment depthStencilAttachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        depthStencilAttachment.view = depthBufferView;
        depthStencilAttachment.depthLoadOp = WGPULoadOp_Clear;
        depthStencilAttachment.depthStoreOp = WGPUStoreOp_Discard;
        depthStencilAttachment.depthClearValue = 1.f;
        depthStencilAttachment.depthReadOnly = WGPU_FALSE;

        WGPURenderPassDescriptor renderPassDescriptor = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        renderPassDescriptor.colorAttachmentCount = 1;
        renderPassDescriptor.colorAttachments = &colorAttachment;
        renderPassDescriptor.depthStencilAttachment = &depthStencilAttachment;
        WGPURenderPassEncoder renderPass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDescriptor);

        wgpuRenderPassEncoderSetPipeline(renderPass, renderPipeline);
        wgpuRenderPassEncoderSetBindGroup(renderPass, 0, cameraBindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(renderPass, 1, lightsBindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(renderPass, 0, vertexBuffer, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderSetIndexBuffer(renderPass, indexBuffer, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);

        math::vector3f const albedo{0.92f, 0.70f, 0.50f};
        float const spacingX = 3.2f;
        float const spacingY = 2.5f;
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                float const x = (static_cast<float>(col) - 1.f) * spacingX;
                float const y = (1.f - static_cast<float>(row)) * spacingY;
                math::matrix4f const model = math::translation(math::vector{x, y, 0.f});
                float const roughness = row == 0 ? 0.05f : (row == 1 ? 0.25f : 0.7f);
                float const specularFactor = 0.5f * static_cast<float>(col);

                Immediates const immediates {
                    .modelTranspose = math::transpose(model),
                    .albedo = albedo,
                    .roughness = roughness,
                    .specularFactor = specularFactor,
                };
                wgpuRenderPassEncoderSetImmediates(renderPass, 0, &immediates, sizeof(immediates));
                wgpuRenderPassEncoderDrawIndexed(renderPass, suzanne.indices.size(), 1, 0, 0, 0);
            }
        }

        wgpuRenderPassEncoderSetPipeline(renderPass, lightRenderPipeline);
        wgpuRenderPassEncoderSetBindGroup(renderPass, 0, cameraBindGroup, 0, nullptr);
        LightImmediates const lightImmediates {
            .position = pointPosition,
        };
        wgpuRenderPassEncoderSetImmediates(renderPass, 0, &lightImmediates, sizeof(lightImmediates));
        wgpuRenderPassEncoderDraw(renderPass, 6, 1, 0, 0);

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

    if (depthBufferView) wgpuTextureViewRelease(depthBufferView);
    if (depthBuffer) wgpuTextureRelease(depthBuffer);

    wgpuBufferRelease(vertexBuffer);
    wgpuBufferRelease(indexBuffer);

    wgpuRenderPipelineRelease(lightRenderPipeline);
    wgpuShaderModuleRelease(lightShaderModule);
    wgpuRenderPipelineRelease(renderPipeline);
    wgpuShaderModuleRelease(shaderModule);
    wgpuBindGroupRelease(cameraBindGroup);
    wgpuBindGroupRelease(lightsBindGroup);
    wgpuBindGroupLayoutRelease(cameraBindGroupLayout);
    wgpuBindGroupLayoutRelease(lightsBindGroupLayout);
    wgpuBufferRelease(cameraUniformBuffer);
    wgpuBufferRelease(lightsUniformBuffer);
} catch (const std::exception &e) {
    std::cerr << "error: " << e.what() << std::endl;
    return EXIT_FAILURE;
}
