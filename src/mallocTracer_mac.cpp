/*
 * Copyright The async-profiler authors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifdef __APPLE__

#include <assert.h>
#include <dlfcn.h>
#include <libkern/OSCacheControl.h>
#include <mach-o/getsect.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/vm_region.h>
#include <malloc/malloc.h>

#include "codeCache.h"
#include "log.h"
#include "mallocTracer.h"
#include "profiler.h"

struct MachoHeader {
  private:
    static struct mach_header_64* self_header;

  public:
    static struct mach_header_64* self_mach_header() {
        if (self_header == nullptr) {
            Dl_info info;
            if (dladdr((void*)self_mach_header, &info) == 0) {
                Log::error("Cannot find self dylib.");
                return nullptr;
            }

            self_header = reinterpret_cast<struct mach_header_64*>(info.dli_fbase);
        }

        return self_header;
    }
};
struct mach_header_64* MachoHeader::self_header = nullptr;

#define DECLARE_HOOK_START(structName, fn, uniqueSection)                                                     \
    struct structName : Hook<decltype(&fn), &fn> {                                                            \
        static void* fnEnd;                                                                                   \
        __attribute__((constructor)) static void init() {                                                     \
            unsigned long fnSize;                                                                             \
            unsigned long secsize;                                                                            \
            getsectiondata(MachoHeader::self_mach_header(), "__TEXT", "__zonehook_" #uniqueSection, &fnSize); \
            assert(fnSize > 0);                                                                               \
            const void* fnStart = (void*)structName::hook;                                                    \
            structName::fnEnd = (void*)((char*)fnStart + fnSize);                                             \
        }                                                                                                     \
        __attribute__((noinline, section("__TEXT,__zonehook_" #uniqueSection)))

#define DECLARE_HOOK_END_NO_ASSERT(structName)                        \
    __attribute__((always_inline)) static inline bool isReentrant() { \
        void* callerPC = __builtin_return_address(0);                 \
        const void* fnStart = (void*)structName::hook;                \
        return callerPC >= fnStart && callerPC < structName::fnEnd;   \
    }                                                                 \
    }                                                                 \
    ;                                                                 \
    void* structName::fnEnd = nullptr

#define DECLARE_HOOK_END(structName, fn)    \
    DECLARE_HOOK_END_NO_ASSERT(structName); \
    ASSERT_HOOK_SIGNATURE(structName, fn)

typedef struct {
    malloc_zone_t* zone;

    // Original vm protection of the zone header. In some cases it is readonly.
    int protection;

    void* (*malloc)(malloc_zone_t* zone, size_t size);

    void* (*calloc)(malloc_zone_t* zone, size_t num_items, size_t size);

    void* (*valloc)(malloc_zone_t* zone, size_t size);

    void (*free)(malloc_zone_t* zone, void* ptr);

    void* (*realloc)(malloc_zone_t* zone, void* ptr, size_t size);

    void (*destroy)(malloc_zone_t* zone);

    unsigned (*batch_malloc)(malloc_zone_t* zone, size_t size, void** results,
                             unsigned num_requested);

    void (*batch_free)(malloc_zone_t* zone, void** to_be_freed,
                       unsigned num_to_be_freed);

    void (*free_definite_size)(malloc_zone_t* zone, void* ptr, size_t size);

    void (*try_free_default)(malloc_zone_t* zone, void* ptr);

    void* (*malloc_with_options)(malloc_zone_t* zone, size_t align, size_t size,
                                 uint64_t options);

    void* (*malloc_type_malloc)(malloc_zone_t* zone, size_t size,
                                malloc_type_id_t type_id);

    void* (*malloc_type_calloc)(malloc_zone_t* zone, size_t count, size_t size,
                                malloc_type_id_t type_id);

    void* (*malloc_type_realloc)(malloc_zone_t* zone, void* ptr, size_t size,
                                 malloc_type_id_t type_id);

    void* (*malloc_type_memalign)(malloc_zone_t* zone, size_t alignment,
                                  size_t size, malloc_type_id_t type_id);

    void* (*malloc_type_malloc_with_options)(malloc_zone_t* zone, size_t align,
                                             size_t size, uint64_t options,
                                             malloc_type_id_t type_id);
} malloc_zone_orig_t;

const int kMaxNumZones = 30;
int origZonesLen = 0;
malloc_zone_orig_t* origZones[kMaxNumZones] = {0};

malloc_zone_orig_t* findZone(malloc_zone_t* zone) {
    for (int i = 0; i < origZonesLen; i++) {
        malloc_zone_orig_t* origZone = origZones[i];
        if (origZone && origZone->zone == zone) {
            return origZone;
        }
    }
    return nullptr;
}

// TODO Need to check some malloc_zone_t fields at compile time if they exist, as they are libmalloc version dependent.

DECLARE_HOOK_START(ZonehookMalloc, malloc_zone_malloc, mzm)
static void* hook(malloc_zone_t* zone, size_t size) {
    malloc_zone_orig_t* origZone = findZone(zone);
    if (!origZone) {
        Log::error("Cannot get original zone for the libmalloc zone: %p\n", zone);
        return nullptr;
    }

    void* ret = origZone->malloc(zone, size);
    if (!isReentrant() && MallocTracer::running() && ret && size) {
        MallocTracer::recordMalloc(ret, size);
    }
    return ret;
}
DECLARE_HOOK_END(ZonehookMalloc, malloc_zone_malloc);

DECLARE_HOOK_START(ZonehookCalloc, malloc_zone_calloc, mc)
static void* hook(malloc_zone_t* zone, size_t count, size_t size) {
    malloc_zone_orig_t* origZone = findZone(zone);
    if (!origZone) {
        Log::error("Cannot get original zone for the libmalloc zone: %p\n", zone);
        return nullptr;
    }

    void* ret = origZone->calloc(zone, count, size);
    if (!isReentrant() && MallocTracer::running() && ret && count && size) {
        MallocTracer::recordMalloc(ret, count * size);
    }
    return ret;
}
DECLARE_HOOK_END(ZonehookCalloc, malloc_zone_calloc);

DECLARE_HOOK_START(ZonehookValloc, malloc_zone_valloc, mzv)
static void* hook(malloc_zone_t* zone, size_t size) {
    malloc_zone_orig_t* origZone = findZone(zone);
    if (!origZone) {
        Log::error("Cannot get original zone for the libmalloc zone: %p\n", zone);
        return nullptr;
    }

    void* ret = origZone->valloc(zone, size);
    if (!isReentrant() && MallocTracer::running() && ret && size) {
        MallocTracer::recordMalloc(ret, size);
    }
    return ret;
}
DECLARE_HOOK_END(ZonehookValloc, malloc_zone_valloc);

DECLARE_HOOK_START(ZonehookFree, malloc_zone_free, mf)
static void hook(malloc_zone_t* zone, void* ptr) {
    malloc_zone_orig_t* origZone = findZone(zone);
    if (!origZone) {
        Log::error("Cannot get original zone for the libmalloc zone: %p\n", zone);
        return;
    }

    origZone->free(zone, ptr);
    if (!isReentrant() && ptr) {
        MallocTracer::recordFree(ptr);
    }
}
DECLARE_HOOK_END(ZonehookFree, malloc_zone_free);

DECLARE_HOOK_START(ZonehookRealloc, malloc_zone_realloc, mr)
static void* hook(malloc_zone_t* zone, void* ptr, size_t size) {
    malloc_zone_orig_t* origZone = findZone(zone);
    if (!origZone) {
        Log::error("Cannot get original zone for the libmalloc zone: %p\n", zone);
        return nullptr;
    }

    void* ret = origZone->realloc(zone, ptr, size);
    if (!isReentrant() && MallocTracer::running()) {
        if (ptr) {
            MallocTracer::recordFree(ptr);
        }
        if (size) {
            MallocTracer::recordMalloc(ret, size);
        }
    }
    return ret;
}
DECLARE_HOOK_END(ZonehookRealloc, malloc_zone_realloc);

// TODO ZonehookDestroy
// TODO ZonehookBatchMalloc
// TODO ZonehookBatchFree

DECLARE_HOOK_START(ZonehookFreeDefiniteSize, malloc_zone_t::free_definite_size, mfds)
static void hook(malloc_zone_t* zone, void* ptr, size_t size) {
    malloc_zone_orig_t* origZone = findZone(zone);
    if (!origZone) {
        Log::error("Cannot get original zone for the libmalloc zone: %p\n", zone);
        return;
    }

    origZone->free_definite_size(zone, ptr, size);
    if (!isReentrant() && ptr && size) {
        MallocTracer::recordFree(ptr);
    }
}
DECLARE_HOOK_END_NO_ASSERT(ZonehookFreeDefiniteSize);

DECLARE_HOOK_START(ZonehookTryFreeDefault, malloc_zone_t::try_free_default, mtfd)
static void hook(malloc_zone_t* zone, void* ptr) {
    malloc_zone_orig_t* origZone = findZone(zone);
    if (!origZone) {
        Log::error("Cannot get original zone for the libmalloc zone: %p\n", zone);
        return;
    }

    origZone->try_free_default(zone, ptr);
    if (!isReentrant() && ptr) {
        MallocTracer::recordFree(ptr);
    }
}
DECLARE_HOOK_END_NO_ASSERT(ZonehookTryFreeDefault);

DECLARE_HOOK_START(ZonehookMallocWithOptions, malloc_zone_t::malloc_with_options, mmo)
static void* hook(malloc_zone_t* zone, size_t align, size_t size, uint64_t options) {
    malloc_zone_orig_t* origZone = findZone(zone);
    if (!origZone) {
        Log::error("Cannot get original zone for the libmalloc zone: %p\n", zone);
        return nullptr;
    }

    void* ret = origZone->malloc_with_options(zone, align, size, options);
    if (!isReentrant() && MallocTracer::running() && ret && size) {
        MallocTracer::recordMalloc(ret, size);
    }
    return ret;
}
DECLARE_HOOK_END_NO_ASSERT(ZonehookMallocWithOptions);

DECLARE_HOOK_START(ZonehookTypedMalloc, malloc_type_zone_malloc, mtm)
static void* hook(malloc_zone_t* zone, size_t size, malloc_type_id_t type_id) {
    malloc_zone_orig_t* origZone = findZone(zone);
    if (!origZone) {
        Log::error("Cannot get original zone for the libmalloc zone: %p\n", zone);
        return nullptr;
    }

    void* ret = origZone->malloc_type_malloc(zone, size, type_id);
    if (!isReentrant() && MallocTracer::running() && ret && size) {
        MallocTracer::recordMalloc(ret, size);
    }
    return ret;
}
DECLARE_HOOK_END(ZonehookTypedMalloc, malloc_type_zone_malloc);

DECLARE_HOOK_START(ZonehookTypedCalloc, malloc_type_zone_calloc, mtc)
static void* hook(malloc_zone_t* zone, size_t count, size_t size, malloc_type_id_t type_id) {
    malloc_zone_orig_t* origZone = findZone(zone);
    if (!origZone) {
        Log::error("Cannot get original zone for the libmalloc zone: %p\n", zone);
        return nullptr;
    }

    void* ret = origZone->malloc_type_calloc(zone, count, size, type_id);
    if (!isReentrant() && MallocTracer::running() && ret && count && size) {
        MallocTracer::recordMalloc(ret, count * size);
    }
    return ret;
}
DECLARE_HOOK_END(ZonehookTypedCalloc, malloc_type_zone_calloc);

DECLARE_HOOK_START(ZonehookTypedRealloc, malloc_type_zone_realloc, mtr)
static void* hook(malloc_zone_t* zone, void* ptr, size_t size, malloc_type_id_t type_id) {
    malloc_zone_orig_t* origZone = findZone(zone);
    if (!origZone) {
        Log::error("Cannot get original zone for the libmalloc zone: %p\n", zone);
        return nullptr;
    }

    void* ret = origZone->malloc_type_realloc(zone, ptr, size, type_id);
    if (!isReentrant() && MallocTracer::running()) {
        if (ptr) {
            MallocTracer::recordFree(ptr);
        }
        if (size) {
            MallocTracer::recordMalloc(ret, size);
        }
    }
    return ret;
}
DECLARE_HOOK_END(ZonehookTypedRealloc, malloc_type_zone_realloc);

DECLARE_HOOK_START(ZonehookTypedMemalign, malloc_zone_t::malloc_type_memalign, mtma)
static void* hook(malloc_zone_t* zone, size_t align, size_t size, malloc_type_id_t type_id) {
    malloc_zone_orig_t* origZone = findZone(zone);
    if (!origZone) {
        Log::error("Cannot get original zone for the libmalloc zone: %p\n", zone);
        return nullptr;
    }

    void* ret = origZone->malloc_type_memalign(zone, align, size, type_id);
    if (!isReentrant() && MallocTracer::running() && ret && size) {
        MallocTracer::recordMalloc(ret, size);
    }
    return ret;
}
DECLARE_HOOK_END_NO_ASSERT(ZonehookTypedMemalign);

DECLARE_HOOK_START(ZonehookTypedMallocWithOptions, malloc_zone_t::malloc_type_malloc_with_options, mtmo)
static void* hook(malloc_zone_t* zone, size_t align, size_t size, uint64_t options, malloc_type_id_t type_id) {
    malloc_zone_orig_t* origZone = findZone(zone);
    if (!origZone) {
        Log::error("Cannot get original zone for the libmalloc zone: %p\n", zone);
        return nullptr;
    }

    void* ret = origZone->malloc_type_malloc_with_options(zone, align, size, options, type_id);
    if (!isReentrant() && MallocTracer::running() && ret && size) {
        MallocTracer::recordMalloc(ret, size);
    }
    return ret;
}
DECLARE_HOOK_END_NO_ASSERT(ZonehookTypedMallocWithOptions);

int get_protection(void* addr) {
    vm_address_t target_addr = (vm_address_t)addr;
    vm_size_t region_size = 0;
    vm_region_flavor_t flavor = VM_REGION_BASIC_INFO_64;
    vm_region_basic_info_data_64_t info;
    mach_msg_type_number_t info_count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object_name;

    kern_return_t kr = vm_region_64(
        mach_task_self(),
        &target_addr,
        &region_size,
        flavor,
        (vm_region_info_t)&info,
        &info_count,
        &object_name);

    if (kr != KERN_SUCCESS) {
        Log::error("Failed to query region at address 0x%lx\n", target_addr);
        return 0;
    }

    return info.protection;
}

malloc_zone_orig_t* saveOrigZone(malloc_zone_t* zone) {
    if (origZonesLen >= kMaxNumZones) {
        Log::error("Cannot hook more libmalloc zones.");
        return nullptr;
    }

    malloc_zone_orig_t* origZone = (malloc_zone_orig_t*)calloc(1, sizeof(malloc_zone_orig_t));

    // TODO if max protection does not have VM_PROT_WRITE, fail early as we won't be able to set it later.
    origZone->zone = zone;
    origZone->protection = get_protection(zone);

    origZone->malloc = zone->malloc;
    origZone->calloc = zone->calloc;
    origZone->valloc = zone->valloc;
    origZone->free = zone->free;
    origZone->realloc = zone->realloc;
    origZone->free_definite_size = zone->free_definite_size;
    origZone->try_free_default = zone->try_free_default;
    origZone->malloc_with_options = zone->malloc_with_options;
    origZone->malloc_type_malloc = zone->malloc_type_malloc;
    origZone->malloc_type_calloc = zone->malloc_type_calloc;
    origZone->malloc_type_realloc = zone->malloc_type_realloc;
    origZone->malloc_type_memalign = zone->malloc_type_memalign;
    origZone->malloc_type_malloc_with_options = zone->malloc_type_malloc_with_options;

    origZones[origZonesLen++] = origZone;

    return origZone;
}

void patchZoneFunctions(malloc_zone_orig_t* origZone, malloc_zone_t* zone) {
    const bool need_reprot = (origZone->protection & VM_PROT_WRITE) == 0;

    // Make callbacks writable by making the entire struct writable, if necessary.
    if (need_reprot) {
        int ret = mach_vm_protect(mach_task_self(), (uintptr_t)zone, sizeof(malloc_zone_t), 0,
                                  origZone->protection | VM_PROT_WRITE);
        if (ret != KERN_SUCCESS) {
            Log::error("Cannot reprotect malloc zone functions.\n");
            return;
        }
    }

    // Patch the function pointers.
    // Hook optionals only when the original is defined.
    zone->malloc = ZonehookMalloc::hook;
    zone->calloc = ZonehookCalloc::hook;
    zone->valloc = ZonehookValloc::hook;
    zone->free = ZonehookFree::hook;
    zone->realloc = ZonehookRealloc::hook;
    zone->free_definite_size = ZonehookFreeDefiniteSize::hook;
    zone->try_free_default = ZonehookTryFreeDefault::hook;

    if (zone->malloc_with_options) {
        zone->malloc_with_options = ZonehookMallocWithOptions::hook;
    }

    zone->malloc_type_malloc = ZonehookTypedMalloc::hook;
    zone->malloc_type_calloc = ZonehookTypedCalloc::hook;
    zone->malloc_type_realloc = ZonehookTypedRealloc::hook;
    zone->malloc_type_memalign = ZonehookTypedMemalign::hook;

    if (zone->malloc_type_malloc_with_options) {
        zone->malloc_type_malloc_with_options = ZonehookTypedMallocWithOptions::hook;
    }

    // Restore protection if necessary.
    if (need_reprot) {
        int ret = mach_vm_protect(mach_task_self(), (uintptr_t)zone, sizeof(malloc_zone_t), 0,
                                  origZone->protection);

        if (ret != KERN_SUCCESS) {
            Log::error("Cannot reprotect malloc zone functions.\n");
            return;
        }
    }

    sys_dcache_flush(zone, sizeof(malloc_zone_t));
}

bool patchZone(malloc_zone_t* zone) {
    malloc_zone_orig_t* origZone = findZone(zone);
    if (origZone) {
        // Already patched.
        return false;
    }

    origZone = saveOrigZone(zone);
    if (!origZone) {
        // Save error, cannot patch.
        return false;
    }

    patchZoneFunctions(origZone, zone);

    return true;
}

void MallocTracer::patchLibraries() {
    MutexLocker ml(_patch_lock);

    malloc_zone_t* default_zone = malloc_default_zone();
    assert(default_zone);
    patchZone(default_zone);

    unsigned int count = 0;
    vm_address_t* addresses = NULL;

    // Get all malloc zones
    kern_return_t result = malloc_get_all_zones(mach_task_self(), NULL, &addresses, &count);
    if (result != KERN_SUCCESS) {
        Log::error("Failed to retrieve malloc zones: %d\n", result);
        return;
    }

    for (unsigned int i = 0; i < count; i++) {
        malloc_zone_t* zone = (malloc_zone_t*)addresses[i];
        assert(zone);
        patchZone(zone);
    }
}

void MallocTracer::initialize() {
    CodeCache* lib = Profiler::instance()->findLibraryByAddress((void*)MallocTracer::initialize);
    assert(lib);

    lib->mark(
        [](const char* s) -> bool {
            // Functions Zonehook<name>::hook
            return strstr(s, "Zonehook") != 0 && strstr(s, "4hook") != 0;
        },
        MARK_ASYNC_PROFILER);
}

#endif // __APPLE__
