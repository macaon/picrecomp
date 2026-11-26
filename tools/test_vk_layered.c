// Synthetic Maxwell geometry shader -> backend graphics pipeline -> every 3D slice / array layer.
// No game data: catches ignored geometry stages, single-layer framebuffers and wrong mip/view ranges.
#include "../runtime/nvn/vulkan/exec.c"
#include <assert.h>
void plat_init(void);
void plat_terminate(void);
void rt_paths_init(void);
void ui_window_init(void);
void renderer_set_scale(float sx, float sy);

static void word(uint64_t *code, unsigned *n, uint64_t w) {
    if (!(*n % 4)) code[(*n)++] = 0; // scheduling slot
    code[(*n)++] = w;
}
static ShaderInfo *geometry(void) {
    uint8_t blob[1024] = {0};
    blob[0x3b] = 1; // one invocation
    blob[0x3f] = 7; // triangle strip
    blob[0x40] = 3; // max vertices
    uint64_t *code = (void *)(blob + 0x80); unsigned n = 0;
    word(code,&n,0xf0c800000007000bull | (29ull<<20)); // S2R invocation info (vertex count in bits 16..23)
    // VSETP P0,PT: count <= 2 must be false for triangle input. A bad count invalidates the vertex below.
    word(code,&n,0x50f0000000070007ull | (11ull<<8) | (2ull<<20) | (5ull<<36) | (7ull<<39) | (3ull<<43));
    for (unsigned v = 0; v < 3; v++) {
        word(code,&n,0x010000000007f004ull | ((uint64_t)v<<20)); // MOV32I R4, vertex
        word(code,&n,0x010000000000f004ull | (0x80000000ull<<20)); // @P0 invalid vertex handle
        word(code,&n,0x010000000017f005ull); // MOV32I R5, 1
        word(code,&n,0x5f00000000070008ull | (4ull<<8) | (5ull<<20) | (6ull<<28) | (6ull<<36) | (255ull<<39) | (1ull<<50)); // VMAD R8,R4,R5,RZ
        word(code,&n,0xefd0000000070808ull); // ISBERD R8,R8 (host vertex index)
        word(code,&n,0xefd800000007ff00ull | (0x70ull<<20) | (8ull<<39) | (3ull<<47)); // ALD.128 R0, position[R8]
        word(code,&n,0xeff000000007ff00ull | (0x70ull<<20) | (3ull<<47)); // AST.128 position,R0
        word(code,&n,0xefd800000007ff09ull | (0x80ull<<20) | (8ull<<39)); // ALD R9, varying0.x[R8]
        word(code,&n,0xeff000000007ff09ull | (0x64ull<<20)); // AST layer,R9
        word(code,&n,0xeff000000007ff09ull | (0x80ull<<20)); // AST varying0.x,R9
        word(code,&n,0xfbe000800ff7ffffull); // OUT emit, stream RZ
    }
    word(code,&n,0xfbe001000ff7ffffull); // OUT cut
    word(code,&n,0xe30000000007000full); // EXIT
    ShaderKey key = {.glsl=1,.geometry_input=3,.vs_out_mask=1,.flip_y=true,.depth_minus_one=true};
    ShaderInfo *si=maxwell_translate(blob,0x80+n*8,STAGE_GEOMETRY,&key);
    if (!si->msl) fprintf(stderr,"%s\n",si->error);
    assert(si->msl && si->writes_layer && si->attr_mask==1 && si->out_mask==1);
    return si;
}
static void check_layers(unsigned target, unsigned mip, unsigned base, unsigned count) {
    NvnTexture t={.width=16,.height=16,.depth=8,.target=target,.flags=8,.format=0x25,.levels=3};
    t.layout.fmt=nvn_format_info(t.format);
    renderer_set_scale(1,1);
    TexBackend *b=vk_tex(&t,true);
    unsigned depth=target==2 ? 8>>mip : 8, extent=16>>mip;
    VkCommandBuffer cb=vk_cmd();vk_full_barrier(cb);
    VkClearColorValue zero={0};
    VkImageSubresourceRange range={VK_IMAGE_ASPECT_COLOR_BIT,0,3,0,b->layers};
    vkCmdClearColorImage(cb,b->image,VK_IMAGE_LAYOUT_GENERAL,&zero,1,&range);
    st.rt=(RtState){.color={&t},.ncolor=1,.has_cview={1},.cview={{.setmask=3,.base_level=mip,.min_layer=base,.num_layers=count}}};
    st.rt_dirty=true;begin_pass();
    assert(vk_render_layers(&t,&st.rt.cview[0],true)==count);
    ShaderInfo vi={.msl="#version 460\nlayout(location=0) out vec4 v0; void main(){vec2 p[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));gl_Position=vec4(p[gl_VertexIndex],-0.5,1);v0=vec4(uintBitsToFloat(uint(gl_InstanceIndex)),0,0,1);}"};
    ShaderInfo fi={.msl="#version 460\nlayout(location=0) flat in vec4 v0;layout(location=0) out vec4 c0;void main(){c0=vec4(float(floatBitsToUint(v0.x)+1u)/16.,0.5,0.75,1);}"};
    Prog prog={.vi=&vi,.fi=&fi,.gi=geometry()};compile_program(&prog);assert(prog.state==1);
    Pipe pipe={.k={.prog=&prog,.color={b->format},.ncolor=1,.samples=1,.topo_class=2,.poly_fill=1,.write_mask={15}}};
    build_pipeline(&pipe);assert(pipe.state==1);
    vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,pipe.pipe);dyn_defaults(cb);
    VkViewport vp={0,0,extent,extent,0,1};VkRect2D sc={{0,0},{extent,extent}};
    vkCmdSetViewport(cb,0,1,&vp);vkCmdSetScissor(cb,0,1,&sc);
    vkCmdDraw(cb,3,count,0,0);vk_end_pass();vk_full_barrier(cb);
    VkSpan read=vk_readback(extent*extent*depth*4);
    VkBufferImageCopy copy={.bufferOffset=read.off,.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,mip,0,target==2?1:depth},.imageExtent={extent,extent,target==2?depth:1}};
    vkCmdCopyImageToBuffer(cb,b->image,VK_IMAGE_LAYOUT_GENERAL,read.buf,1,&copy);renderer_flush();
    uint8_t *pixels=read.ptr;
    for(unsigned z=0;z<depth;z++) for(unsigned i=0;i<extent*extent;i++) {
        uint8_t *p=pixels+4*(z*extent*extent+i);
        if(z>=base && z<base+count) {
            unsigned red=(unsigned)lroundf((z-base+1)*255.f/16.f);
            if(abs((int)p[0]-(int)red)>1 || abs((int)p[1]-128)>1 || p[2]!=191 || p[3]!=255) {
                fprintf(stderr,"target %u mip %u slice %u pixel %u: %u %u %u %u expected red %u\n",target,mip,z,i,p[0],p[1],p[2],p[3],red);abort();
            }
        } else assert(p[0]==0 && p[1]==0 && p[2]==0);
    }
    vkDestroyPipeline(g_vk.dev,pipe.pipe,NULL);vkDestroyPipelineLayout(g_vk.dev,prog.layout,NULL);
    for(int i=0;i<3;i++) vkDestroyDescriptorSetLayout(g_vk.dev,prog.set_layout[i],NULL);
    vkDestroyShaderModule(g_vk.dev,prog.vs,NULL);vkDestroyShaderModule(g_vk.dev,prog.fs,NULL);vkDestroyShaderModule(g_vk.dev,prog.gs,NULL);
    maxwell_free(prog.gi);backend_texture_destroyed(&t);
    st.rt=(RtState){0};st.rt_dirty=true;
    printf("Layered geometry: target %u mip %u layers %u..%u passed\n",target,mip,base,base+count-1);
}
int main(void) {
    plat_init();setvbuf(stderr,NULL,_IOLBF,0);setvbuf(stdout,NULL,_IOLBF,0);rt_paths_init();ui_window_init();
    check_layers(2,0,0,8);check_layers(2,1,1,2);check_layers(2,2,1,1);
    check_layers(4,0,0,8);check_layers(4,1,2,3);
    puts("Vulkan layered geometry execution passed (no game data)");fflush(stderr);plat_terminate();
}
