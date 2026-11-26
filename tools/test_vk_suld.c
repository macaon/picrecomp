// Synthetic Maxwell -> GLSL -> Vulkan execution, including the runtime's raw buffer-view selection. No game data.
#include "../runtime/nvn/vulkan/exec.c"
#include "../runtime/paths.h"
#include <assert.h>

void ui_window_init(void);
void renderer_flush(void);

static void check_shared_handle(void) {
    uint8_t code[0x180] = {0};
    *(uint32_t *)code = 0x12345679;
    uint64_t words[] = {0, 0x4c98078804870009ull, 0x010000000107f003ull,
        0xeb10048200470300ull, 0, 0xeb20048200f70400ull,
        0xe30000000007000full, 0xe2400fffff87000full};
    memcpy(code+0x100,words,sizeof words);
    ShaderKey key={.glsl=1};
    ShaderInfo *si=maxwell_translate(code,sizeof code,STAGE_COMPUTE,&key);
    assert(si->msl && si->ntex==2 && si->tex[0].img_kind==5 && si->tex[1].img_kind==1);
    assert(si->tex[0].bindless_offset==si->tex[1].bindless_offset);
    maxwell_free(si);
    // Unsupported dimensions and sub-word sizes must not silently use the 1D word-load path.
    words[3]=(words[3]&~(UINT64_C(7)<<33))|(UINT64_C(3)<<33);
    memcpy(code+0x100,words,sizeof words);
    si=maxwell_translate(code,sizeof code,STAGE_COMPUTE,&key);
    assert(!si->msl);maxwell_free(si);
    words[3]=0xeb10048200170300ull; // raw sub-word load: not implemented
    memcpy(code+0x100,words,sizeof words);
    si=maxwell_translate(code,sizeof code,STAGE_COMPUTE,&key);
    assert(!si->msl);maxwell_free(si);
    puts("SULD raw load and formatted store share a handle; unsupported forms rejected");
}

static void check_load(unsigned size, unsigned offset, unsigned format) {
    uint8_t code[0x180] = {0};
    *(uint32_t *)code = 0x12345679;
    uint64_t words[] = {0,
        0x4c98078804870009ull, // MOV R9, c[2][0x120]: synthetic image handle
        0x010000000007f003ull | (uint64_t)offset << 20, // MOV32I R3, byte offset
        0xeb10000000070000ull | UINT64_C(1) << 33 | UINT64_C(9) << 39 | (uint64_t)size << 20 | 3u << 8,
        0, 0xe30000000007000full, 0xe2400fffff87000full, 0xe2400fffff87000full};
    memcpy(code + 0x100, words, sizeof words);
    ShaderKey key = {.glsl=1};
    ShaderInfo *si = maxwell_translate(code, sizeof code, STAGE_COMPUTE, &key);
    if (!si->msl) fprintf(stderr,"%s\n",si->error);
    assert(si->msl && si->ntex == 1 && si->tex[0].img_kind == 5);
    unsigned n = 1u << (size - 2);
    // Instrument only the synthetic shader's register outputs; its load and declarations are the translator's.
    const char *entry = strstr(si->msl,"void main("), *end = strrchr(si->msl,'}');
    assert(entry && end && entry < end);
    char *src = malloc(strlen(si->msl) + 512);
    snprintf(src,strlen(si->msl)+512,"%.*s\nlayout(set=0,binding=80,std430) buffer Probe { uvec4 result; } probe;\n%.*s"
        "probe.result=uvec4(r0,%s,%s,%s);\n}\n",(int)(entry-si->msl),si->msl,(int)(end-entry),entry,
        n>1?"r1":"0u",n>2?"r2":"0u",n>3?"r3":"0u");
    size_t nw;
    uint32_t *spv = vk_glsl_to_spirv(src,STAGE_COMPUTE,"suld-test",&nw);
    assert(spv);
    VkShaderModule module = vk_module(spv,nw);
    free(spv); free(src);
    uint32_t data[16];
    for (unsigned i=0;i<16;i++) data[i]=0x10000000u+i;
    data[4]=0x3f800000;data[5]=0xdeadbeef;data[6]=0x7fc00001;data[7]=0x80000000;
    uint32_t zero[128]={0};
    VkSpan input=vk_transient_copy(data,sizeof data,256), uniform=vk_transient_copy(zero,sizeof zero,256),
        output=vk_transient_copy(zero,16,256);
    NvnTexture texture={.target=10,.format=format,.addr=HEAP_BASE+input.off};
    texture.layout.fmt=nvn_format_info(format);
    texture.width=sizeof data/vk_texel_bytes(&texture);
    NvnTexEntry entries[2]={{0},{.tex=&texture}};
    NvnDescPool pool={.entries=entries,.count=2};
    st.tpool=&pool;st.img[5][0]=1;
    // Substitute a small real GPU allocation for the heap import. The view resolver still sees the guest address.
    gmem_buf=input.buf;gmem_size=input.off+sizeof data;
    int written_before=g_ngpu_written;
    VkBufferView view=texel_view_for(5,si,0);
    assert(view && g_ngpu_written==written_before);
    gmem_buf=VK_NULL_HANDLE;gmem_size=0;
    VkDescriptorSetLayoutBinding bindings[] = {
        {.binding=2,.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding=32,.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding=80,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT}};
    VkDescriptorSetLayoutCreateInfo sl={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=3,.pBindings=bindings};
    VkDescriptorSetLayout set_layout;VK_CHECK(vkCreateDescriptorSetLayout(g_vk.dev,&sl,NULL,&set_layout));
    VkPipelineLayoutCreateInfo pl={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&set_layout};
    VkPipelineLayout layout;VK_CHECK(vkCreatePipelineLayout(g_vk.dev,&pl,NULL,&layout));
    VkComputePipelineCreateInfo cp={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.layout=layout,
        .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=module,.pName="main"}};
    VkPipeline pipeline;VK_CHECK(vkCreateComputePipelines(g_vk.dev,VK_NULL_HANDLE,1,&cp,NULL,&pipeline));
    VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1},{VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,1},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1}};
    VkDescriptorPoolCreateInfo pi={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=3,.pPoolSizes=sizes};
    VkDescriptorPool dp;VK_CHECK(vkCreateDescriptorPool(g_vk.dev,&pi,NULL,&dp));
    VkDescriptorSetAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=dp,.descriptorSetCount=1,.pSetLayouts=&set_layout};
    VkDescriptorSet set;VK_CHECK(vkAllocateDescriptorSets(g_vk.dev,&ai,&set));
    VkDescriptorBufferInfo ub={uniform.buf,uniform.off,sizeof zero}, ob={output.buf,output.off,16};
    VkWriteDescriptorSet writes[3];
    for(int i=0;i<3;i++) writes[i]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,
        .dstBinding=bindings[i].binding,.descriptorCount=1,.descriptorType=bindings[i].descriptorType};
    writes[0].pBufferInfo=&ub;writes[1].pTexelBufferView=&view;writes[2].pBufferInfo=&ob;
    vkUpdateDescriptorSets(g_vk.dev,3,writes,0,NULL);
    VkCommandBuffer cb=vk_cmd();vk_full_barrier(cb);
    vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
    vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,NULL);
    vkCmdDispatch(cb,1,1,1);vk_full_barrier(cb);
    VkSpan read=vk_readback(16);
    VkBufferCopy copy={output.off,read.off,16};vkCmdCopyBuffer(cb,output.buf,read.buf,1,&copy);
    renderer_flush();
    uint32_t *actual=read.ptr;
    for(unsigned i=0;i<4;i++) assert(actual[i]==(i<n && offset/4+i<16?data[offset/4+i]:0));
    vkDestroyDescriptorPool(g_vk.dev,dp,NULL);vkDestroyPipeline(g_vk.dev,pipeline,NULL);
    vkDestroyPipelineLayout(g_vk.dev,layout,NULL);vkDestroyDescriptorSetLayout(g_vk.dev,set_layout,NULL);
    vkDestroyShaderModule(g_vk.dev,module,NULL);maxwell_free(si);
    printf("SULD U%u byte offset %u, NVN format %#x: raw register values passed\n",32*n,offset,format);
}

int main(void) {
    plat_init();setvbuf(stderr,NULL,_IOLBF,0);setvbuf(stdout,NULL,_IOLBF,0);
    rt_paths_init();ui_window_init();
    check_shared_handle();
    for(unsigned format=0;format<2;format++) {
        unsigned f=format?0x07:0x2e; // R16_FLOAT and RGBA32_FLOAT: neither defines the raw load width/type
        check_load(2,4,f);check_load(3,8,f);check_load(4,16,f);check_load(4,128,f);
    }
    puts("Vulkan SULD execution passed (no game data)");fflush(stdout);fflush(stderr);plat_terminate();
}
