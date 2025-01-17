/*
 * Copyright The async-profiler authors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <jni.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef __APPLE__
#include <malloc/malloc.h>
#endif

const int ARBITRARY_TYPE_ID = 12349990;
const int ARBITRARY_ALIGNMENT = sizeof(void*) * 4;
const int ARBITRARY_OPTIONS = 0;

JNIEXPORT jlong JNICALL Java_test_nativemem_Native_malloc(JNIEnv* env, jclass clazz, jlong size) {
    void* ptr = malloc((size_t)size);
    return (jlong)(intptr_t)ptr;
}

JNIEXPORT jlong JNICALL Java_test_nativemem_Native_calloc(JNIEnv* env, jclass clazz, jlong num, jlong size) {
    void* ptr = calloc(num, (size_t)size);
    return (jlong)(intptr_t)ptr;
}

JNIEXPORT jlong JNICALL Java_test_nativemem_Native_realloc(JNIEnv* env, jclass clazz, jlong addr, jlong size) {
    void* ptr = realloc((void*)(intptr_t)addr, (size_t)size);
    return (jlong)(intptr_t)ptr;
}

JNIEXPORT void JNICALL Java_test_nativemem_Native_free(JNIEnv* env, jclass clazz, jlong addr) {
    free((void*)(intptr_t)addr);
}

#ifdef __APPLE__

JNIEXPORT jlong JNICALL Java_test_nativemem_Native_typeRealloc(JNIEnv* env, jclass clazz, jlong addr, jlong size) {
    malloc_zone_t* default_zone = malloc_default_zone();
    void* ptr = malloc_type_zone_realloc(default_zone, (void*)(intptr_t)addr, (size_t)size, ARBITRARY_TYPE_ID);
    return (jlong)(intptr_t)ptr;
}

JNIEXPORT jlong JNICALL Java_test_nativemem_Native_typeMemalign(JNIEnv* env, jclass clazz, jlong size) {
    malloc_zone_t* default_zone = malloc_default_zone();
    void* ptr = malloc_type_zone_memalign(default_zone, ARBITRARY_ALIGNMENT, (size_t)size, ARBITRARY_TYPE_ID);
    return (jlong)(intptr_t)ptr;
}

JNIEXPORT jlong JNICALL Java_test_nativemem_Native_zoneMalloc(JNIEnv* env, jclass clazz, jlong size) {
    malloc_zone_t* default_zone = malloc_default_zone();

    // malloc_zone_malloc falls back to malloc_type_zone_malloc, unless zone->version is >= 13 && < 16.
    // Version is readonly, so we use zone->malloc directly.
    void* ptr = default_zone->malloc(default_zone, (size_t)size);
    return (jlong)(intptr_t)ptr;
}

JNIEXPORT jlong JNICALL Java_test_nativemem_Native_zoneCalloc(JNIEnv* env, jclass clazz, jlong num, jlong size) {
    malloc_zone_t* default_zone = malloc_default_zone();

    // malloc_zone_calloc falls back to malloc_type_zone_calloc, unless zone->version is >= 13 && < 16.
    // Version is readonly, so we use zone->calloc directly.
    void* ptr = default_zone->calloc(default_zone, num, (size_t)size);
    return (jlong)(intptr_t)ptr;
}

JNIEXPORT jlong JNICALL Java_test_nativemem_Native_zoneValloc(JNIEnv* env, jclass clazz, jlong size) {
    malloc_zone_t* default_zone = malloc_default_zone();
    void* ptr = malloc_zone_valloc(default_zone, (size_t)size);
    return (jlong)(intptr_t)ptr;
}

JNIEXPORT void JNICALL Java_test_nativemem_Native_freeDefiniteSize(JNIEnv* env, jclass clazz, jlong addr, jlong size) {
    malloc_zone_t* default_zone = malloc_default_zone();
    default_zone->free_definite_size(default_zone, (void*)(intptr_t)addr, (size_t)size);
}

#endif // __APPLE__
