// Real GPU regression of storage promotion; uses the runtime backend, without game data.
#include "../runtime/nvn/vulkan/textures.c"
#include "../runtime/paths.h"
#include <assert.h>
#include <shaderc/shaderc.h>

void ui_window_init(void);
void renderer_flush(void);

static void store_corner(TexBackend *b, int target) {
    const char *type = target == 2 ? "image3D" : target == 4 ? "image2DArray" : "image2D";
    const char *coord = target == 2 ? "ivec3(imageSize(dst)-1)" : target == 4 ? "ivec3(imageSize(dst)-1)" : "ivec2(imageSize(dst)-1)";
    char src[512];
    snprintf(src, sizeof src, "#version 450\nlayout(local_size_x=1) in;\n"
             "layout(rgba8,binding=0) uniform %s dst;\nvoid main(){ imageStore(dst,%s,vec4(1,0,0,1)); }\n", type, coord);
    shaderc_compiler_t compiler = shaderc_compiler_initialize();
    shaderc_compilation_result_t result = shaderc_compile_into_spv(compiler, src, strlen(src), shaderc_compute_shader,
                                                                 "storage-test", "main", NULL);
    assert(shaderc_result_get_compilation_status(result) == shaderc_compilation_status_success);
    VkShaderModuleCreateInfo sm = {.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize=shaderc_result_get_length(result), .pCode=(const uint32_t *)shaderc_result_get_bytes(result)};
    VkShaderModule module;
    VK_CHECK(vkCreateShaderModule(g_vk.dev, &sm, NULL, &module));
    shaderc_result_release(result); shaderc_compiler_release(compiler);
    VkDescriptorSetLayoutBinding binding = {.binding=0, .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .descriptorCount=1, .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT};
    VkDescriptorSetLayoutCreateInfo sl = {.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount=1, .pBindings=&binding};
    VkDescriptorSetLayout set_layout;
    VK_CHECK(vkCreateDescriptorSetLayout(g_vk.dev, &sl, NULL, &set_layout));
    VkPipelineLayoutCreateInfo pl = {.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount=1, .pSetLayouts=&set_layout};
    VkPipelineLayout layout;
    VK_CHECK(vkCreatePipelineLayout(g_vk.dev, &pl, NULL, &layout));
    VkComputePipelineCreateInfo cp = {.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, .layout=layout,
        .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage=VK_SHADER_STAGE_COMPUTE_BIT, .module=module, .pName="main"}};
    VkPipeline pipeline;
    VK_CHECK(vkCreateComputePipelines(g_vk.dev, VK_NULL_HANDLE, 1, &cp, NULL, &pipeline));
    VkDescriptorPoolSize ps = {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1};
    VkDescriptorPoolCreateInfo pc = {.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets=1, .poolSizeCount=1, .pPoolSizes=&ps};
    VkDescriptorPool pool;
    VK_CHECK(vkCreateDescriptorPool(g_vk.dev, &pc, NULL, &pool));
    VkDescriptorSetAllocateInfo sa = {.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool=pool, .descriptorSetCount=1, .pSetLayouts=&set_layout};
    VkDescriptorSet set;
    VK_CHECK(vkAllocateDescriptorSets(g_vk.dev, &sa, &set));
    VkComponentMapping identity = {VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY,
                                   VK_COMPONENT_SWIZZLE_IDENTITY,VK_COMPONENT_SWIZZLE_IDENTITY};
    VkImageView view = vk_image_view(b, b->base_type, b->format, 0, 1, 0, b->layers, identity, b->aspect);
    VkDescriptorImageInfo image = {.imageView=view, .imageLayout=VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet write = {.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet=set, .dstBinding=0,
        .descriptorCount=1, .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .pImageInfo=&image};
    vkUpdateDescriptorSets(g_vk.dev, 1, &write, 0, NULL);
    VkCommandBuffer cb = vk_cmd();
    vk_full_barrier(cb);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0, NULL);
    vkCmdDispatch(cb, 1, 1, 1);
    renderer_flush();
    vkDestroyDescriptorPool(g_vk.dev, pool, NULL);
    vkDestroyPipeline(g_vk.dev, pipeline, NULL);
    vkDestroyPipelineLayout(g_vk.dev, layout, NULL);
    vkDestroyDescriptorSetLayout(g_vk.dev, set_layout, NULL);
    vkDestroyShaderModule(g_vk.dev, module, NULL);
}

static void check_storage(int target, float scale) {
    NvnTexture t = {.width=192, .height=108, .depth=target == 2 ? 4 : target == 4 ? 3 : 1,
        .target=(uint32_t)target, .flags=8, .format=0x25, .levels=3};
    t.layout.fmt = nvn_format_info(t.format);
    renderer_set_scale(scale, scale);
    TexBackend *b = vk_tex(&t, true);
    assert(b->width == (target == 1 ? (uint32_t)lroundf(192*scale) : 192));
    // Shader variants follow a view override, not just the texture's original format.
    NvnTextureView view = {.setmask=4, .format=0x27};
    assert(vk_sampled_format(&t, &view, true) == VK_FORMAT_R8G8B8A8_UINT);
    view.format = 0x28;
    assert(vk_sampled_format(&t, &view, true) == VK_FORMAT_R8G8B8A8_SINT);
    assert(vk_sampled_format(&t, &view, false) == VK_FORMAT_R8G8B8A8_UNORM);
    // Record a GPU write without flushing: promotion must preserve it after the main buffer executes.
    VkClearColorValue clear = {.float32={0.25f,0.5f,0.75f,1}};
    VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, b->levels, 0, b->layers};
    VkCommandBuffer cb = vk_cmd();
    vk_full_barrier(cb);
    vkCmdClearColorImage(cb, b->image, VK_IMAGE_LAYOUT_GENERAL, &clear, 1, &range);
    b = vk_tex_storage(&t);
    assert(!b->scalable && b->width==192 && b->height==108 && b->storage==1);
    float sx, sy; renderer_texture_scale(&t,&sx,&sy); assert(sx==1 && sy==1);
    store_corner(b,target);
    for (uint32_t lv=0; lv<b->levels; lv++) {
        uint32_t w=level_w(b,lv), h=level_h(b,lv), d=target==2 ? (b->depth>>lv ? b->depth>>lv : 1) : 1;
        size_t size=(size_t)w*h*d*b->layers*4;
        VkSpan out=vk_readback(size);
        VkBufferImageCopy copy={.bufferOffset=out.off,
            .imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,lv,0,b->layers}, .imageExtent={w,h,d}};
        cb=vk_cmd();vk_full_barrier(cb);
        vkCmdCopyImageToBuffer(cb,b->image,VK_IMAGE_LAYOUT_GENERAL,out.buf,1,&copy);
        renderer_flush();
        const uint8_t *px=out.ptr;
        for (size_t i=0;i<size;i+=4) {
            if (lv==0 && i==size-4) assert(px[i]==255 && !px[i+1] && !px[i+2] && px[i+3]==255);
            else assert(px[i]>=63 && px[i]<=64 && px[i+1]>=127 && px[i+1]<=128 &&
                        px[i+2]>=191 && px[i+2]<=192 && px[i+3]==255);
        }
    }
    renderer_set_scale(scale*1.25f,scale*1.25f);
    assert(vk_tex(&t,true)==b && b->width==192 && b->height==108); // Storage images stay in the guest grid.
    vk_textures_reset();
    vk_collect_dead();
    printf("storage promotion: target %d scale %.2f, pixels and compute store passed\n",target,scale);
}

int main(void) {
    plat_init();
    setvbuf(stderr,NULL,_IOLBF,0);
    setvbuf(stdout,NULL,_IOLBF,0);
    rt_paths_init();ui_window_init();
    unsetenv("PICRECOMP_NO_SCALE");
    check_storage(1,0.5f);check_storage(1,1.0f);check_storage(1,1.5f);
    check_storage(2,0.5f);check_storage(4,0.5f);
    puts("Vulkan storage promotion passed (no game data)");fflush(stdout);
    fflush(stderr);
    // Match the runtime's exit path; SDL/UI static destructors expect its event loop to own teardown.
    plat_terminate();
}
