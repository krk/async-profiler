/*
 * Copyright The async-profiler authors
 * SPDX-License-Identifier: Apache-2.0
 */

package one.convert;

import one.elf.ElfReader;
import one.elf.ElfSection;
import one.elf.ElfSymbol;
import one.elf.ElfSymbolTable;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * Resolves native program counter addresses to symbol names by lazy-loading ELF symbol tables.
 * Maintains a streaming list of libraries that can be updated as NativeLibrary events are encountered.
 */
public class SymbolResolver {
    public static class LibraryInfo {
        public final String name;
        public final long baseAddress;
        public final long topAddress;

        public LibraryInfo(String name, long baseAddress, long topAddress) {
            this.name = name;
            this.baseAddress = baseAddress;
            this.topAddress = topAddress;
        }
    }

    private static class SymbolInfo {
        final String name;
        final long value;
        final long size;

        SymbolInfo(String name, long value, long size) {
            this.name = name;
            this.value = value;
            this.size = size;
        }
    }

    private final List<LibraryInfo> libraries = new ArrayList<>();
    private final Map<String, List<SymbolInfo>> symbolCache = new HashMap<>();

    /**
     * Add a library to the resolver using insertion sort to maintain sorted order.
     * Libraries can be added dynamically as NativeLibrary events are encountered.
     *
     * @param name Library name/path
     * @param baseAddress Base address of the library
     * @param topAddress Top address of the library
     */
    public void addLibrary(String name, long baseAddress, long topAddress) {
        System.err.println("DEBUG: addLibrary: " + name + " [0x" + Long.toHexString(baseAddress) + " - 0x" + Long.toHexString(topAddress) + "]");

        LibraryInfo newLib = new LibraryInfo(name, baseAddress, topAddress);
        int i = libraries.size();
        while (i > 0 && libraries.get(i - 1).baseAddress > baseAddress) {
            i--;
        }
        libraries.add(i, newLib);
    }

    /**
     * Resolves a program counter address to a symbol name.
     *
     * @param pc The program counter address
     * @return Formatted symbol string in the form "library`symbol+offset" or "library@+offset"
     */
    public String resolve(long pc) {
        // Binary search to find the library containing this PC
        LibraryInfo lib = findLibrary(pc);
        if (lib == null) {
            System.err.println("DEBUG: resolve() called with pc=0x" + Long.toHexString(pc) + ", libraries.size()=" + libraries.size());
            System.err.println("DEBUG: No library found for pc=0x" + Long.toHexString(pc));
            return "0x" + Long.toHexString(pc);
        }

        // Extract basename from library path
        String basename = getBasename(lib.name);

        // Calculate offset from library base
        long offset = pc - lib.baseAddress;

        // Load symbols for this library (lazy, cached)
        List<SymbolInfo> symbols = loadSymbols(lib.name);

        if (symbols == null || symbols.isEmpty()) {
            System.err.println("DEBUG: resolve() called with pc=0x" + Long.toHexString(pc) + ", libraries.size()=" + libraries.size());
            System.err.println("DEBUG: No symbols found for pc=0x" + Long.toHexString(pc));

            // No symbols available, use base+offset format
            return basename + "@+0x" + Long.toHexString(offset);
        }

        // Binary search to find closest symbol <= offset
        SymbolInfo symbol = binarySearchFloor(symbols, offset, s -> s.value);

        if (symbol == null) {
            // PC before first symbol
            return basename + "@+0x" + Long.toHexString(offset);
        }

        // Calculate offset from symbol start
        long symbolOffset = offset - symbol.value;

        // Note: We don't enforce symbol.size checks because .dynsym often has
        // incorrect/incomplete size information (especially in stripped libraries).
        // Instead, we trust the "closest symbol" heuristic, matching the behavior
        // of tools like addr2line and objdump which use .eh_frame for boundaries.

        // Return symbol+offset format
        if (symbolOffset == 0) {
            return basename + "`" + symbol.name;
        } else {
            return basename + "`" + symbol.name + "+0x" + Long.toHexString(symbolOffset);
        }
    }

    /**
     * Find the library containing the given PC address using binary search.
     */
    private LibraryInfo findLibrary(long pc) {
        LibraryInfo candidate = binarySearchFloor(libraries, pc, lib -> lib.baseAddress);

        // Verify the candidate contains the PC (within baseAddress and topAddress)
        if (candidate != null && pc >= candidate.baseAddress && pc <= candidate.topAddress) {
            return candidate;
        }

        return null;
    }

    /**
     * Generic binary search to find the largest element with key <= target.
     *
     * @param list Sorted list to search
     * @param target Target value to search for
     * @param keyExtractor Function to extract the search key from an element
     * @return Element with largest key <= target, or null if no such element exists
     */
    private <T> T binarySearchFloor(List<T> list, long target, KeyExtractor<T> keyExtractor) {
        int left = 0;
        int right = list.size() - 1;
        T candidate = null;

        while (left <= right) {
            int mid = (left + right) >>> 1;
            T element = list.get(mid);
            long key = keyExtractor.getKey(element);

            if (target < key) {
                right = mid - 1;
            } else {
                candidate = element;
                left = mid + 1;
            }
        }

        return candidate;
    }

    @FunctionalInterface
    private interface KeyExtractor<T> {
        long getKey(T element);
    }

    /**
     * Lazy load symbols from an ELF file. Results are cached.
     * Merges symbols from both .symtab and .dynsym sections for maximum coverage.
     */
    private List<SymbolInfo> loadSymbols(String path) {
        // Check cache first
        if (symbolCache.containsKey(path)) {
            return symbolCache.get(path);
        }

        List<SymbolInfo> symbols = new ArrayList<>();

        try {
            File file = new File(path);
            if (!file.exists() || !file.canRead()) {
                // Cache empty result to avoid repeated failed reads
                symbolCache.put(path, symbols);
                return symbols;
            }

            ElfReader reader = new ElfReader(file);

            // Use a map to deduplicate symbols by (name, value) pair
            // This handles cases where the same symbol appears in both .symtab and .dynsym
            Map<String, SymbolInfo> symbolMap = new HashMap<>();

            // Load symbols from .symtab if available
            ElfSection symtabSection = reader.section(".symtab");
            if (symtabSection instanceof ElfSymbolTable) {
                extractSymbols((ElfSymbolTable) symtabSection, symbolMap);
            }

            // Load symbols from .dynsym if available
            ElfSection dynsymSection = reader.section(".dynsym");
            if (dynsymSection instanceof ElfSymbolTable) {
                extractSymbols((ElfSymbolTable) dynsymSection, symbolMap);
            }

            // Convert map to list
            symbols.addAll(symbolMap.values());

            // Sort symbols by value for binary search
            symbols.sort((a, b) -> Long.compare(a.value, b.value));

        } catch (IOException e) {
            // Silently fail - library might be stripped or inaccessible
        }

        // Cache result (even if empty)
        symbolCache.put(path, symbols);
        return symbols;
    }

    /**
     * Extract symbols from a symbol table and add to the map.
     * Uses name+value as key to avoid duplicates.
     */
    private void extractSymbols(ElfSymbolTable symtab, Map<String, SymbolInfo> symbolMap) {
        for (ElfSymbol symbol : symtab) {
            String name = symbol.name();
            // Only include function symbols with names
            if (name != null && !name.isEmpty() &&
                (symbol.type() == ElfSymbol.STT_FUNC || symbol.type() == ElfSymbol.STT_NOTYPE)) {

                // Use name+value as key to deduplicate
                String key = name + "@" + symbol.value();

                // If duplicate, prefer the one with larger size (more information)
                SymbolInfo existing = symbolMap.get(key);
                if (existing == null || symbol.size() > existing.size) {
                    symbolMap.put(key, new SymbolInfo(name, symbol.value(), symbol.size()));
                }
            }
        }
    }

    /**
     * Extract the basename from a file path.
     */
    private String getBasename(String path) {
        if (path == null || path.isEmpty()) {
            return "unknown";
        }
        int lastSlash = path.lastIndexOf('/');
        return lastSlash >= 0 ? path.substring(lastSlash + 1) : path;
    }
}
