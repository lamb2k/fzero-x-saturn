#ifndef FZX_ROAD_STREAM_PHASE_H
#define FZX_ROAD_STREAM_PHASE_H
/* FZX_ROAD_STREAM_PHASES=2 is the road-slave profile: the same marks, without the
 * dead-chunk diagnostic loops of =1 (which would add their own time to QFETCH). */
/* Default-off C3 per-phase diagnostic (FZX_ROAD_STREAM_PHASES=1, native
 * frame-profile builds only). Disabled, every macro expands to nothing and
 * the renderer's code is unchanged. Enabled, the road scene is partitioned
 * into exclusive phases: each switch adds the 16-bit FRT delta since the
 * previous switch to the outgoing phase. Single intervals stay far below
 * 65536 ticks; per-frame sums are 32-bit. Counts are per presented frame.
 * Timing is MODELLED (emulator) and includes interrupts taken in a phase. */
#ifndef FZX_ROAD_STREAM_PHASES
#define FZX_ROAD_STREAM_PHASES 0
#endif
enum {
    FZX_RSP_OTHER,FZX_RSP_EYE,FZX_RSP_GATHER,FZX_RSP_BATCH,FZX_RSP_QUAD,
    FZX_RSP_CLIP,FZX_RSP_EDGE,FZX_RSP_MERGE,FZX_RSP_FOG,FZX_RSP_BUILD,
    FZX_RSP_CONVERT,FZX_RSP_REPORT,
    /* Version 2: quad setup split. QFETCH is record/vertex fetch and the
     * trivial reject, QCULL the whole-quad winding test, QPIECE the per-piece
     * reject and corner selection; QUAD keeps texture setup and tiering. */
    FZX_RSP_QFETCH,FZX_RSP_QCULL,FZX_RSP_QPIECE,
    /* Version 3 (E0): cluster bounds tests (FZX_ROAD_STREAM_CLUSTERS). */
    FZX_RSP_CLUSTER,
    /* Version 4 (road-slave profile, FZX_ROAD_STREAM_PHASES=2): the slave's pick
     * (before the road build), a piece's guard clip, the row-clip strip plan,
     * the unseen-strip-quad test and the per-frame fog preparation. */
    FZX_RSP_PICK,FZX_RSP_PCLIP,FZX_RSP_STRIP,FZX_RSP_UNSEEN,FZX_RSP_FPREP,
    /* the near-plane clip and the 2D side clips inside lean_clip / lean_clip_ts */
    FZX_RSP_PNEAR,FZX_RSP_PSIDE,FZX_RSP_PHASES
};
enum {
    FZX_RSPC_BOUNDARIES,FZX_RSPC_STEPPED,FZX_RSPC_QUAD_CLIPS,FZX_RSPC_PIECE_CLIPS,
    FZX_RSPC_CLIP_CORNERS,FZX_RSPC_PIECES,FZX_RSPC_MERGES,FZX_RSPC_EDGE_CALLS,
    FZX_RSPC_SWITCHES,
    /* Version 3 (E0): cull reasons and what chunk- or strip-level bounds could
     * drop: quads culled by a shared outcode, by winding, by an empty clip;
     * chunks and (chunk, strip) groups with no surviving quad, and their quads. */
    FZX_RSPC_CULL_OUT,FZX_RSPC_CULL_WIND,FZX_RSPC_CULL_CLIP,
    FZX_RSPC_DEAD_CHUNKS,FZX_RSPC_DEAD_CHUNK_QUADS,FZX_RSPC_DEAD_STRIPS,FZX_RSPC_DEAD_STRIP_QUADS,
    /* clusters tested, quads skipped by a culled cluster, and (audit builds)
     * skipped quads that would have drawn: must stay zero. */
    FZX_RSPC_CLUSTER_TESTS,FZX_RSPC_CLUSTER_SKIPS,FZX_RSPC_CLUSTER_FALSE,
    /* Version 4: strip quads examined and strip quads the unseen test dropped. */
    FZX_RSPC_STRIPS,FZX_RSPC_UNSEEN,
    FZX_RSPC_COUNTERS
};
#if FZX_ROAD_STREAM_PHASES
#include <stdint.h>
typedef struct {
    uint32_t ticks[FZX_RSP_PHASES];
    uint16_t counts[FZX_RSPC_COUNTERS];
    uint16_t last;
    uint8_t phase;
} fzx_rsp_state_t;
extern fzx_rsp_state_t fzx_rsp;
static inline uint16_t fzx_rsp_now(void){
    /* SH7604 FRC: reading FRCH latches FRCL. */
    const uint16_t high=*(volatile uint8_t *)0xfffffe12u;
    const uint16_t low=*(volatile uint8_t *)0xfffffe13u;
    return (uint16_t)(high<<8|low);
}
static inline void fzx_rsp_set(unsigned phase){
    const uint16_t now=fzx_rsp_now();
    fzx_rsp.ticks[fzx_rsp.phase]+=(uint16_t)(now-fzx_rsp.last);
    fzx_rsp.last=now;fzx_rsp.phase=(uint8_t)phase;++fzx_rsp.counts[FZX_RSPC_SWITCHES];
}
/* Scene-4 entry: the caller's float eye conversion is the first phase. */
static inline void fzx_rsp_begin(void){
    for(unsigned i=0;i<FZX_RSP_PHASES;++i)fzx_rsp.ticks[i]=0;
    for(unsigned i=0;i<FZX_RSPC_COUNTERS;++i)fzx_rsp.counts[i]=0;
    fzx_rsp.phase=FZX_RSP_EYE;fzx_rsp.last=fzx_rsp_now();
}
/* After fzx_rsp_begin() at the end of the slave's pick: the pick's own ticks (from its t0). */
#define FZX_RSP_PICK_DONE(t0) (fzx_rsp.ticks[FZX_RSP_PICK]=(uint16_t)(fzx_rsp_now()-(uint16_t)(t0)))
#define FZX_RSP(p) fzx_rsp_set(FZX_RSP_##p)
#define FZX_RSP_COUNT(c,n) (fzx_rsp.counts[FZX_RSPC_##c]+=(uint16_t)(n))
#else
#define FZX_RSP(p) ((void)0)
#define FZX_RSP_COUNT(c,n) ((void)0)
#define FZX_RSP_PICK_DONE(t0) ((void)0)
#endif
#endif
