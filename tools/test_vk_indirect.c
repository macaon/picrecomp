// GPU-produced indirect arguments, execution-time reads, indexed offsets and no-op semantics. No game data.
#include "../runtime/nvn/vulkan/exec.c"
#include "../runtime/paths.h"
#include <assert.h>
void ui_window_init(void);
void renderer_set_scale(float sx,float sy);

static void emit(uint64_t *code, unsigned *n, uint64_t w) {
    if (!(*n % 4)) code[(*n)++]=0;
    code[(*n)++]=w;
}
static void check_decode(void) {
    CmdRec r={.op=CMD_DRAW_ELEMENTS_INDIRECT,.x={4,1,1000,2000}}, out;
    uint32_t a[]={6,4,3,(uint32_t)-2,7};
    assert(expand_indirect(&r,a,&out));
    assert(out.op==CMD_DRAW_ELEMENTS && out.x[2]==6 && out.x[3]==1006 && (int64_t)out.x[4]==-2 && out.x[5]==7 && out.x[6]==4);
    r.x[1]=2;assert(expand_indirect(&r,a,&out) && out.x[3]==1012);
    r.op=CMD_DRAW_ARRAYS_INDIRECT;
    assert(expand_indirect(&r,a,&out) && out.x[1]==3 && out.x[2]==6 && out.x[3]==(uint32_t)-2 && out.x[4]==4);
    a[1]=0;assert(!expand_indirect(&r,a,&out));a[1]=1;a[0]=0;assert(!expand_indirect(&r,a,&out));
    r.op=CMD_DISPATCH_INDIRECT;a[0]=2;a[1]=3;a[2]=4;
    assert(expand_indirect(&r,a,&out) && out.op==CMD_DISPATCH && out.x[0]==2 && out.x[1]==3 && out.x[2]==4);
    a[2]=0;assert(!expand_indirect(&r,a,&out));
}
static void check_draw(bool indexed) {
    uint8_t vs[0x100]={0}, fs[0x100]={0};unsigned n=0;
    emit((void *)(vs+0x80),&n,0xefd800000007ff00ull | (0x80ull<<20) | (255ull<<39) | (3ull<<47));
    emit((void *)(vs+0x80),&n,0xeff000000007ff00ull | (0x70ull<<20) | (3ull<<47));
    emit((void *)(vs+0x80),&n,0xe30000000007000full);
    n=0;uint32_t color[]={0x3f800000,0x3e800000,0,0x3f800000};
    for(unsigned i=0;i<4;i++) emit((void *)(fs+0x80),&n,0x010000000007f000ull | ((uint64_t)color[i]<<20) | i);
    emit((void *)(fs+0x80),&n,0xe30000000007000full);*(uint32_t *)(fs+0x78)=15;
    NvnProgram program={.stage_mask=3};
    program.stages[0].code_addr=(uintptr_t)vs;program.stages[0].const_offset=sizeof vs;
    program.stages[1].code_addr=(uintptr_t)fs;program.stages[1].const_offset=sizeof fs;
    NvnTexture target={.target=1,.width=8,.height=8,.depth=1,.levels=1,.format=0x25,.flags=8};
    target.layout.fmt=nvn_format_info(target.format);renderer_set_scale(1,1);
    st_defaults();st.program=&program;st.rt=(RtState){.ncolor=1,.color={&target}};st.rt_dirty=true;
    st.viewport[0][2]=st.viewport[0][3]=8;st.scissor[0][2]=st.scissor[0][3]=8;st.depth_range[0][1]=1;
    st.nattrib=st.nstream=1;st.attrib[0]=0x2e;st.stream[0][0]=16;
    float vertices[4][4]={{0,0,0,1},{-1,-1,0,1},{3,-1,0,1},{-1,3,0,1}};
    uint16_t indices[]={0,0,2,3,4}; // firstIndex=2, baseVertex=-1 => vertices 1,2,3
    st.vbuf[0]=(Range){(uintptr_t)vertices,sizeof vertices};
    uint32_t zero[5]={0};VkSpan arguments=vk_transient_copy(zero,sizeof zero,256);
    // The indirect arguments start as zeros. A real compute dispatch produces them immediately before execution.
    const char *src="#version 460\nlayout(local_size_x=1) in;layout(set=0,binding=0,std430) buffer Args{uint a[];};"
        "void main(){a[0]=3;a[1]=1;a[2]=2;a[3]=0xffffffffu;a[4]=2;}";
    size_t nw;uint32_t *spv=vk_glsl_to_spirv(src,STAGE_COMPUTE,"indirect-args",&nw);assert(spv);
    VkShaderModule module=vk_module(spv,nw);free(spv);
    VkBindingInfo binding={0,0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER};
    VkDescriptorSetLayout sl=vk_set_layout(&binding,1,0,VK_SHADER_STAGE_COMPUTE_BIT);
    VkPipelineLayoutCreateInfo pl={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&sl};
    VkPipelineLayout layout;VK_CHECK(vkCreatePipelineLayout(g_vk.dev,&pl,NULL,&layout));
    VkComputePipelineCreateInfo ci={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.layout=layout,
        .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=module,.pName="main"}};
    VkPipeline pipeline;VK_CHECK(vkCreateComputePipelines(g_vk.dev,VK_NULL_HANDLE,1,&ci,NULL,&pipeline));
    VkDescriptorSet set=vk_alloc_set(sl);VkDescriptorBufferInfo bi={arguments.buf,arguments.off,20};
    VkWriteDescriptorSet write={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=0,.descriptorCount=1,
        .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&bi};vkUpdateDescriptorSets(g_vk.dev,1,&write,0,NULL);
    VkCommandBuffer cb=vk_cmd();vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
    vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,NULL);vkCmdDispatch(cb,1,1,1);
    CmdRec r={.op=CMD_DRAW_ELEMENTS_INDIRECT,.x={4,1,(uintptr_t)indices,(uintptr_t)arguments.ptr}};
    if(!indexed) {
        // Arrays use {count,instances,first,baseInstance}; a separate GPU copy supplies first=1.
        uint32_t first=1;VkSpan one=vk_transient_copy(&first,4,4);VkBufferCopy copy={one.off,arguments.off+8,4};
        vk_full_barrier(cb);vkCmdCopyBuffer(cb,one.buf,arguments.buf,1,&copy);
        r=(CmdRec){.op=CMD_DRAW_ARRAYS_INDIRECT,.x={4,(uintptr_t)arguments.ptr}};
    }
    indirect(&r);vk_end_pass();vk_full_barrier(vk_cmd());
    TexBackend *b=target.backend;VkSpan pixels=vk_readback(8*8*4);
    VkBufferImageCopy copy={.bufferOffset=pixels.off,.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={8,8,1}};
    vkCmdCopyImageToBuffer(vk_cmd(),b->image,VK_IMAGE_LAYOUT_GENERAL,pixels.buf,1,&copy);renderer_flush();
    for(unsigned i=0;i<64;i++) { uint8_t *p=(uint8_t *)pixels.ptr+4*i;assert(p[0]==255 && abs((int)p[1]-64)<=1 && p[2]==0 && p[3]==255); }
    backend_texture_destroyed(&target);st.rt=(RtState){0};st.rt_dirty=true;
    vkDestroyPipeline(g_vk.dev,pipeline,NULL);vkDestroyPipelineLayout(g_vk.dev,layout,NULL);vkDestroyDescriptorSetLayout(g_vk.dev,sl,NULL);vkDestroyShaderModule(g_vk.dev,module,NULL);
    vk_exec_reset();
    printf("GPU-generated %s indirect draw passed\n",indexed?"indexed":"array");
}
static void check_dispatch(void) {
    uint32_t zeros[3]={0};VkSpan args=vk_transient_copy(zeros,12,256), result=vk_transient_copy(zeros,4,256);
    char src[300];snprintf(src,sizeof src,"#version 460\nlayout(local_size_x=1) in;layout(set=0,binding=17,std430) buffer Out{uint w[];};void main(){atomicAdd(w[%llu],1u);}",(unsigned long long)(result.off/4));
    size_t nw;uint32_t *spv=vk_glsl_to_spirv(src,STAGE_COMPUTE,"indirect-dispatch",&nw);assert(spv);
    ShaderInfo si={.stage=STAGE_COMPUTE};CsProg cp={.si=&si,.local={1,1,1},.p={.vi=&si,.state=1}};
    cp.p.nbinds=vk_reflect(spv,nw,cp.p.binds,MAX_BIND);VkShaderModule module=vk_module(spv,nw);free(spv);
    cp.p.layout=make_layout(&cp.p,1,VK_SHADER_STAGE_COMPUTE_BIT,0);
    VkComputePipelineCreateInfo ci={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.layout=cp.p.layout,
        .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=module,.pName="main"}};
    VK_CHECK(vkCreateComputePipelines(g_vk.dev,VK_NULL_HANDLE,1,&ci,NULL,&cp.pipe));
    NvnProgram np={.stage_mask=1u<<5};np.stages[5].code_addr=1;st.program=&np;
    g_cs_tab[0]=(struct CsEnt){.addr=1,.cp=&cp};g_cs_n=1;
    gmem_buf=result.buf;gmem_size=result.off+4;
    VkCommandBuffer cb=vk_cmd();vkCmdFillBuffer(cb,args.buf,args.off,12,2); // GPU produces groups 2x2x2
    CmdRec r={.op=CMD_DISPATCH_INDIRECT,.x={(uintptr_t)args.ptr}};indirect(&r);renderer_flush();
    assert(*(uint32_t *)result.ptr==8);
    *(uint32_t *)args.ptr=0;indirect(&r);renderer_flush();assert(*(uint32_t *)result.ptr==8);
    gmem_buf=VK_NULL_HANDLE;gmem_size=0;g_cs_n=0;st.program=NULL;
    vkDestroyPipeline(g_vk.dev,cp.pipe,NULL);vkDestroyPipelineLayout(g_vk.dev,cp.p.layout,NULL);
    vkDestroyDescriptorSetLayout(g_vk.dev,cp.p.set_layout[0],NULL);vkDestroyShaderModule(g_vk.dev,module,NULL);
    puts("GPU-generated indirect dispatch and zero-group no-op passed");
}
int main(void) {
    plat_init();setvbuf(stdout,NULL,_IOLBF,0);setvbuf(stderr,NULL,_IOLBF,0);rt_paths_init();ui_window_init();
    setenv("PICRECOMP_ASYNC_SHADERS","0",1);check_decode();check_draw(true);check_draw(false);check_dispatch();
    puts("Vulkan indirect execution passed (no game data)");fflush(stderr);plat_terminate();
}
