/*
 * Copyright The async-profiler authors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifdef __linux__

#include "mallocTracer.h"

int MallocTracer::_patched_libs = 0;

void MallocTracer::initialize() {
    CodeCache* lib = Profiler::instance()->findLibraryByAddress((void*)MallocTracer::initialize);
    assert(lib);

    lib->mark(
        [](const char* s) -> bool {
            return strcmp(s, "malloc_hook") == 0
                || strcmp(s, "calloc_hook") == 0
                || strcmp(s, "realloc_hook") == 0
                || strcmp(s, "free_hook") == 0;
        },
        MARK_ASYNC_PROFILER);
}

void MallocTracer::patchLibraries() {
    MutexLocker ml(_patch_lock);

    CodeCacheArray* native_libs = Profiler::instance()->nativeLibs();
    int native_lib_count = native_libs->count();

    while (_patched_libs < native_lib_count) {
        CodeCache* cc = (*native_libs)[_patched_libs++];

        cc->patchImport(im_malloc, (void*)malloc_hook);
        cc->patchImport(im_calloc, (void*)calloc_hook);
        cc->patchImport(im_realloc, (void*)realloc_hook);
        cc->patchImport(im_free, (void*)free_hook);
    }
}

#endif // __linux__
