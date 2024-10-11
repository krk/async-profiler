/*
 * Copyright The async-profiler authors
 * SPDX-License-Identifier: Apache-2.0
 */

package one.profiler.test;

import java.io.File;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.*;
import java.util.logging.Handler;
import java.util.logging.Level;
import java.util.logging.Logger;


public class Runner {
    private static final Logger log = Logger.getLogger(Runner.class.getName());

    private static final Os currentOs = detectOs();
    private static final Arch currentArch = detectArch();
    private static final Jvm currentJvm = detectJvm();
    private static final int currentJvmVersion = detectJvmVersion();

    private static final Set<String> skipTests = new HashSet<>();
    private static final String logDir = System.getProperty("logDir", "");

    private static Os detectOs() {
        String os = System.getProperty("os.name").toLowerCase();
        if (os.contains("linux")) {
            return Os.LINUX;
        } else if (os.contains("mac")) {
            return Os.MACOS;
        } else if (os.contains("windows")) {
            return Os.WINDOWS;
        }
        throw new IllegalStateException("Unknown OS type");
    }

    private static Arch detectArch() {
        String arch = System.getProperty("os.arch");
        if (arch.contains("x86_64") || arch.contains("amd64")) {
            return Arch.X64;
        } else if (arch.contains("aarch64")) {
            return Arch.ARM64;
        } else if (arch.contains("arm")) {
            return Arch.ARM32;
        } else if (arch.contains("ppc64le")) {
            return Arch.PPC64LE;
        } else if (arch.contains("riscv64")) {
            return Arch.RISCV64;
        } else if (arch.contains("loongarch64")) {
            return Arch.LOONGARCH64;
        } else if (arch.endsWith("86")) {
            return Arch.X86;
        }
        throw new IllegalStateException("Unknown CPU architecture");
    }

    private static Jvm detectJvm() {
        // Example javaHome: /usr/lib/jvm/amazon-corretto-17.0.8.7.1-linux-x64
        File javaHome = new File(System.getProperty("java.home"));

        // Look for OpenJ9-specific file
        File[] files = new File(javaHome, "lib").listFiles();
        if (files != null) {
            for (File file : files) {
                if (file.getName().equals("J9TraceFormat.dat")) {
                    return Jvm.OPENJ9;
                }
            }
        }

        // Strip /jre from JDK 8 path
        if (currentJvmVersion <= 8) {
            javaHome = javaHome.getParentFile();
        }

        // Workaround for Contents/Home on macOS
        if (currentOs == Os.MACOS) {
            javaHome = javaHome.getParentFile();
        }

        // Look for Zing-specific file
        if (new File(javaHome, "etc/zing").exists()) {
            return Jvm.ZING;
        }

        // Otherwise it's some variation of HotSpot
        return Jvm.HOTSPOT;
    }

    private static int detectJvmVersion() {
        String prop = System.getProperty("java.vm.specification.version");
        if (prop.startsWith("1.")) {
            prop = prop.substring(2);
        }
        return Integer.parseInt(prop);
    }

    public static boolean enabled(Test test) {
        if (!test.enabled()) {
            return false;
        }

        Os[] os = test.os();
        if (os.length > 0 && !Arrays.asList(os).contains(currentOs)) {
            return false;
        }

        Arch[] arch = test.arch();
        if (arch.length > 0 && !Arrays.asList(arch).contains(currentArch)) {
            return false;
        }

        Jvm[] jvm = test.jvm();
        if (jvm.length > 0 && !Arrays.asList(jvm).contains(currentJvm)) {
            return false;
        }

        int[] jvmVer = test.jvmVer();
        if (jvmVer.length > 0 && (currentJvmVersion < jvmVer[0] || currentJvmVersion > jvmVer[jvmVer.length - 1])) {
            return false;
        }

        return true;
    }

    private static TestResult run(RunnableTest rt) {
        if (!enabled(rt.test()) || skipTests.contains(rt.className().toLowerCase()) || skipTests.contains(rt.method().getName().toLowerCase())) {
            return TestResult.skip();
        }

        log.log(Level.INFO, "Running " + rt.testInfo() + "...");

        String testLogDir = logDir.isEmpty() ? null : logDir + '/' + rt.testName();
        try (TestProcess p = new TestProcess(rt.test(), currentOs, testLogDir)) {
            Object holder = (rt.method().getModifiers() & Modifier.STATIC) == 0 ?
                    rt.method().getDeclaringClass().getDeclaredConstructor().newInstance() : null;
            rt.method().invoke(holder, p);
        } catch (InvocationTargetException e) {
            return TestResult.fail(e.getTargetException());
        } catch (Throwable e) {
            return TestResult.fail(e);
        }

        return TestResult.pass();
    }

    private static List<RunnableTest> getRunnableTests(Class<?> cls) {
        List<RunnableTest> rts = new ArrayList<>();
        for (Method m : cls.getMethods()) {
            for (Test t : m.getAnnotationsByType(Test.class)) {
                rts.add(new RunnableTest(m, t));
            }
        }
        return rts;
    }

    private static List<RunnableTest> getRunnableTests(String[] args) throws ClassNotFoundException {
        List<RunnableTest> rts = new ArrayList<>();
        for (String arg : args) {
            String testName = arg;
            if (testName.indexOf('.') < 0 && Character.isLowerCase(testName.charAt(0))) {
                // Convert package name to class name
                testName = "test." + testName + "." + Character.toUpperCase(testName.charAt(0)) + testName.substring(1) + "Tests";
            }
            rts.addAll(getRunnableTests(Class.forName(testName)));
        }
        return rts;
    }

    private static void configureLogging() {
        if (System.getProperty("java.util.logging.ConsoleHandler.formatter") == null) {
            System.setProperty("java.util.logging.ConsoleHandler.formatter", "java.util.logging.SimpleFormatter");
        }
        if (System.getProperty("java.util.logging.SimpleFormatter.format") == null) {
            System.setProperty("java.util.logging.SimpleFormatter.format", "%4$s: %5$s%6$s%n");
        }

        String logLevelProperty = System.getProperty("logLevel");
        if (logLevelProperty != null && !logLevelProperty.isEmpty()) {
            Level level = Level.parse(logLevelProperty);
            Logger logger = Logger.getLogger("");
            logger.setLevel(level);
            for (Handler handler : logger.getHandlers()) {
                handler.setLevel(level);
            }
        }
    }

    private static void configureSkipTests() {
        String skipProperty = System.getProperty("skip");
        if (skipProperty != null && !skipProperty.isEmpty()) {
            for (String skip : skipProperty.split(",")) {
                skipTests.add(skip.toLowerCase());
            }
        }
    }

    private static String formatTestMessage(RunnableTest rt, TestResult result, int i, int testCount, long durationNs) {
        String duration = String.format("%.1f ms", durationNs / 1e6);
        String message = result.status().toString() + " [" + i + "/" + testCount + "] " + rt.testInfo() + " took " + duration;
        if (result.throwable() != null) {
            message += ": " + result.throwable();
        }
        return message;
    }

    private static void printSummary(Map<TestStatus, Integer> statusCounts, List<String> failedTests, long totalTestDuration, int testCount) {
        int fail = statusCounts.getOrDefault(TestStatus.FAIL, 0);
        if (fail > 0) {
            System.out.println("\nFailed tests:");
            failedTests.forEach(System.out::println);
        }

        String totalDuration = String.format("%.1f ms", totalTestDuration / 1e6);
        System.out.println("\nTotal test duration: " + totalDuration);
        System.out.println("Results Summary:");
        System.out.println("PASS: " + statusCounts.getOrDefault(TestStatus.PASS, 0));
        System.out.println("SKIP: " + statusCounts.getOrDefault(TestStatus.SKIP, 0));
        System.out.println("FAIL: " + fail);
        System.out.println("TOTAL: " + testCount);
    }

    public static void main(String[] args) throws Exception {
        if (args.length == 0) {
            System.out.println("Usage: java " + Runner.class.getName() + " TestName ...");
            System.exit(1);
        }

        configureLogging();
        configureSkipTests();

        List<RunnableTest> allTests = getRunnableTests(args);
        final int testCount = allTests.size();

        int i = 1;
        long totalTestDuration = 0;
        Map<TestStatus, Integer> statusCounts = new HashMap<>();
        List<String> failedTests = new ArrayList<>();
        for (RunnableTest rt : allTests) {
            long start = System.nanoTime();
            TestResult result = run(rt);
            long durationNs = System.nanoTime() - start;

            statusCounts.put(result.status(), statusCounts.getOrDefault(result.status(), 0) + 1);
            totalTestDuration += durationNs;
            String message = formatTestMessage(rt, result, i, testCount, durationNs);
            if (result.status() == TestStatus.FAIL) {
                failedTests.add(rt.testInfo());
            }
            System.out.println(message);
            i++;
        }

        printSummary(statusCounts, failedTests, totalTestDuration, testCount);

        if (!logDir.isEmpty()) {
            log.log(Level.INFO, "Test output and profiles are available in " + logDir + " directory");
        }

        if (!failedTests.isEmpty()) {
            throw new RuntimeException("One or more tests failed");
        }
    }
}
