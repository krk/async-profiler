/*
 * Copyright The async-profiler authors
 * SPDX-License-Identifier: Apache-2.0
 */

package one.jfr.event;

import one.jfr.JfrReader;

import java.io.IOException;

public class NativeLibrary extends Event {
    public final String name;
    public final long baseAddress;
    public final long topAddress;

    public NativeLibrary(JfrReader jfr) throws IOException {
        super(jfr.getVarlong(), 0, 0);  // startTime, no tid or stackTraceId
        this.name = jfr.getString();
        this.baseAddress = jfr.getVarlong();
        this.topAddress = jfr.getVarlong();
    }
}
