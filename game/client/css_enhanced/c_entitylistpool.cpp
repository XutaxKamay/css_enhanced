#include "cbase.h"
#include "c_entitylistpool.h"

// this memory pool stores blocks around the size of C_EventAction/inputitem_t structs
// can be used for other blocks; will error if to big a block is tried to be allocated
CUtlMemoryPool g_EntityListPool( MAX(sizeof(C_EventAction), sizeof(C_MultiInputVar::inputitem_t)), 512, CUtlMemoryPool::GROW_FAST, "g_EntityListPool" );
