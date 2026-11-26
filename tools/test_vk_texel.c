// Formatted TLD buffer reads: half conversion, normalization, integer values and descriptor variant selection. No game data.
#include "../runtime/nvn/vulkan/exec.c"
#include "../runtime/paths.h"
#include <assert.h>

void ui_window_init(void);
void renderer_flush(void);

static void check_load(unsigned format, unsigned kind, uint32_t bits0, uint32_t bits1, const uint32_t expected[4]) {
    uint8_t code[0x180] = {0};
    *(uint32_t *)code = 0x12345679;
    uint64_t words[] = {0, 0x010000000027f008ull, // MOV32I R8, texel 2
        0xdc38000000070000ull | (8ull<<8) | (255ull<<20) | (15ull<<31) | (8ull<<36), // TLD R0..3, buffer slot 0
        0xe30000000007000full};
    memcpy(code+0x100,words,sizeof words);
    ShaderKey key={.glsl=1,.buf_mask=1,.uint_tex_mask=kind==1,.sint_tex_mask=kind==2};
    ShaderInfo *si=maxwell_translate(code,sizeof code,STAGE_COMPUTE,&key);
    if (!si->msl) fprintf(stderr,"%s\n",si->error);
    assert(si->msl && si->ntex==1 && si->tex[0].type==TEXTYPE_BUFFER);
    unsigned n=4;
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
    uint32_t data[16]={0}, zero[128]={0};
    NvnTexture texture={.target=10,.format=format};
    texture.layout.fmt=nvn_format_info(format);
    unsigned bytes=vk_texel_bytes(&texture);
    memcpy((uint8_t *)data+2*bytes,&bits0,4);
    if(bytes>4) memcpy((uint8_t *)data+2*bytes+4,&bits1,4);
    VkSpan input=vk_transient_copy(data,sizeof data,256), uniform=vk_transient_copy(zero,sizeof zero,256), output=vk_transient_copy(zero,16,256);
    texture.addr=HEAP_BASE+input.off;texture.width=sizeof data/bytes;
    NvnTexEntry entries[2]={{0},{.tex=&texture}};
    NvnDescPool pool={.entries=entries,.count=2};st.tpool=&pool;st.tex[5][0]=1;
    // Verify that the descriptor format selects the same sampler numeric type as the shader.
    ShaderKey sampled={0};sampled_texture_key(5,si,&sampled);
    assert(sampled.uint_tex_mask==key.uint_tex_mask && sampled.sint_tex_mask==key.sint_tex_mask);
    gmem_buf=input.buf;gmem_size=input.off+sizeof data;
    VkBufferView view=texel_view_for(5,si,0);assert(view);
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
    for(unsigned i=0;i<4;i++) {
        if(kind==0) { float x,y;memcpy(&x,&actual[i],4);memcpy(&y,&expected[i],4);assert(fabsf(x-y)<0.00001f); }
        else assert(actual[i]==expected[i]);
    }
    vkDestroyDescriptorPool(g_vk.dev,dp,NULL);vkDestroyPipeline(g_vk.dev,pipeline,NULL);
    vkDestroyPipelineLayout(g_vk.dev,layout,NULL);vkDestroyDescriptorSetLayout(g_vk.dev,set_layout,NULL);
    vkDestroyShaderModule(g_vk.dev,module,NULL);maxwell_free(si);
    printf("Formatted TLD buffer: NVN format %#x, numeric type %u passed\n",format,kind);
}

int main(void) {
    plat_init();setvbuf(stderr,NULL,_IOLBF,0);setvbuf(stdout,NULL,_IOLBF,0);
    rt_paths_init();ui_window_init();
    const uint32_t half[]={0x3f000000,0xc0000000,0,0x3f800000}; // 0.5, -2, default B=0 A=1
    const uint32_t unorm[]={0x3f800000,0,0,0x3f800000};
    const uint32_t snorm[]={0xbf800000,0x3f800000,0,0x3f800000};
    const uint32_t uints[]={1,128,254,255}, sints[]={-32768,32767,-2,3};
    check_load(0x11,0,0xc0003800,0,half); // RG16_FLOAT
    check_load(0x25,0,0xff0000ff,0,unorm); // RGBA8_UNORM
    check_load(0x26,0,0x7f007f80,0,snorm); // RGBA8_SNORM
    check_load(0x27,1,0xfffe8001,0,uints); // RGBA8_UINT
    check_load(0x2d,2,0x7fff8000,0x0003fffe,sints); // RGBA16_SINT
    puts("Vulkan formatted buffer reads passed (no game data)");fflush(stdout);fflush(stderr);plat_terminate();
}
