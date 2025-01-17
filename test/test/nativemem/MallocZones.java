/*
 * Copyright The async-profiler authors
 * SPDX-License-Identifier: Apache-2.0
 */

package test.nativemem;

// MacOS-only.
public class MallocZones {

    private static final int NUM_THREADS = 8; // Number of threads

    private static final int MALLOC_SIZE = 1999993; // Prime size, useful in assertions.
    private static final int CALLOC_SIZE = 2000147;
    private static final int REALLOC_SIZE = 30000170;

    private static void do_work(boolean once) {
        try {
            do {
                long addr = Native.malloc(MALLOC_SIZE);
                long reallocd = Native.typeRealloc(addr, REALLOC_SIZE);
                Native.free(reallocd);

                addr = Native.typeMemalign(MALLOC_SIZE);
                Native.free(addr);

                addr = Native.zoneMalloc(MALLOC_SIZE);
                Native.free(addr);

                addr = Native.zoneMalloc(MALLOC_SIZE);
                Native.free(addr);

                addr = Native.zoneCalloc(1, CALLOC_SIZE);
                Native.free(addr);

                addr = Native.zoneValloc(MALLOC_SIZE);
                Native.freeDefiniteSize(addr, MALLOC_SIZE);

                Thread.sleep(1);
            } while (!once);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            System.err.println("Thread interrupted: " + Thread.currentThread().getName());
        }
    }

    public static void main(String[] args) throws InterruptedException {
        String osName = System.getProperty("os.name").toLowerCase();
        if (!osName.contains("mac")) {
            System.err.println("This application is only supported on macOS.");
            System.exit(1);
        }

        final boolean once = args.length > 0 && args[0].equals("once");

        final Thread[] threads = new Thread[NUM_THREADS];
        for (int i = 0; i < NUM_THREADS; i++) {
            threads[i] = new Thread(() -> do_work(once), "MemoryTask-" + i);
            threads[i].start();
        }

        for (Thread thread : threads) {
            thread.join();
        }
    }
}
