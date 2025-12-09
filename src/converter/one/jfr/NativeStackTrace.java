/*
 * Copyright The async-profiler authors
 * SPDX-License-Identifier: Apache-2.0
 */

package one.jfr;

public class NativeStackTrace {
    public final boolean truncated;
    public final long[] pcs;

    public NativeStackTrace(boolean truncated, long[] pcs) {
        this.truncated = truncated;
        this.pcs = pcs;
    }
}
