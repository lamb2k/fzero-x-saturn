#ifndef FZX_PALETTE_METRICS_H
#define FZX_PALETTE_METRICS_H
static uint32_t hash_word(uint32_t h,uint16_t word)
{return ((h^(word>>8))*16777619u^(word&255u))*16777619u;}
static uint32_t hash_vertices(const fzx_model_vertex_t *v,unsigned count)
{
    uint32_t h=2166136261u;
    for(unsigned i=0;i<count;++i){
        const uint16_t words[]={(uint16_t)v[i].x_q4,(uint16_t)v[i].y_q4,v[i].inverse_z,
            (uint16_t)v[i].u_q5,(uint16_t)v[i].v_q5,v[i].shade.r,v[i].shade.g,v[i].shade.b,v[i].shade.a};
        for(unsigned j=0;j<9;++j)h=hash_word(h,words[j]);
    }
    return h;
}
#endif
