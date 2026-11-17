#pragma once
#include <stdint.h>

// Diagnostic only, one writer per vCPU, inspected with ALL threads stopped.
// Record every overflow recovery, not just slow ones; zero records then has
// meaning. Never use these observations to skip a write or invalidation.
#define ENGINE_NOTE_RESCAN_SAMPLES 64
typedef struct {
    uint64_t sequence, start_ticks, duration_ticks, code_frames, table_frames;
} EngineNoteRescanSample;
typedef struct {
    volatile uint64_t active_start_ticks;
    uint64_t total;
    EngineNoteRescanSample samples[ENGINE_NOTE_RESCAN_SAMPLES];
} EngineNoteRescanRing;

static inline void engine_note_rescan_record(EngineNoteRescanRing *ring,
    uint64_t start, uint64_t duration, uint64_t code, uint64_t tables) {
    uint64_t sequence=ring->total+1;
    EngineNoteRescanSample *sample=&ring->samples[(sequence-1)%ENGINE_NOTE_RESCAN_SAMPLES];
    sample->sequence=0;
    sample->start_ticks=start;sample->duration_ticks=duration;
    sample->code_frames=code;sample->table_frames=tables;
    __atomic_store_n(&sample->sequence,sequence,__ATOMIC_RELEASE);
    ring->total=sequence;
    ring->active_start_ticks=0;
}
