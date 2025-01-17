/*
 * Copyright The async-profiler authors
 * SPDX-License-Identifier: Apache-2.0
 */

package test.nativemem;

public class Native {
    static {
        System.loadLibrary("jnimalloc");
    }

    public static native long malloc(int size);

    public static native long realloc(long addr, int size);

    public static native long calloc(long num, int size);

    public static native void free(long addr);

    // MacOS-only.
    public static native long typeRealloc(long addr, int size);

    // MacOS-only.
    public static native long typeCalloc(long num, int size);

    // MacOS-only.
    public static native long typeMemalign(int size);

    // MacOS-only.
    public static native long zoneMalloc(int size);

    // MacOS-only.
    public static native long zoneCalloc(long num, int size);

    // MacOS-only.
    public static native long zoneValloc(int size);

    // MacOS-only.
    public static native void freeDefiniteSize(long addr, int size);
}
