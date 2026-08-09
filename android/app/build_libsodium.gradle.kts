/*
 * build_libsodium.gradle.kts
 *
 * Gradle script that automatically downloads, extracts, and cross-compiles
 * libsodium for Android (arm64-v8a) using the NDK toolchain.
 *
 * Applied from app/build.gradle.kts.  Runs before the CMake / native build
 * so that third_party/libsodium/lib/arm64-v8a/libsodium.a is always present.
 *
 * If libsodium.a already exists the task is a no-op (UP-TO-DATE).
 */

val thirdPartyDir  = file("src/main/cpp/third_party")
val sodiumOutputDir = file("$thirdPartyDir/libsodium")            // include/ + lib/
val sodiumLib       = file("$sodiumOutputDir/lib/arm64-v8a/libsodium.a")

val sodiumVersion   = "1.0.18"
val tarballName     = "libsodium-${sodiumVersion}-stable.tar.gz"
val tarballUrl      = "https://download.libsodium.org/libsodium/releases/old/$tarballName"

// Temporary work directory (inside the project, so Gradle can track it)
val sodiumWorkDir   = File(project.buildDir, "libsodium-build")

// ---------------------------------------------------------------------------
// Resolve the Android NDK root
// ---------------------------------------------------------------------------
fun resolveNdkDir(): File {
    // 1. ndk.dir from local.properties  (Gradle convention)
    val localProps = rootProject.file("local.properties")
    if (localProps.exists()) {
        val props = java.util.Properties().apply { localProps.inputStream().use { load(it) } }
        val sdkDir = props.getProperty("sdk.dir")
        if (sdkDir != null) {
            // Look for the NDK inside the SDK
            val ndkBundle = File(sdkDir, "ndk-bundle")
            if (ndkBundle.isDirectory) return ndkBundle
            // Or pick the newest versioned NDK
            val ndkDir = File(sdkDir, "ndk")
            if (ndkDir.isDirectory) {
                val newest = ndkDir.listFiles()?.filter { it.isDirectory }?.maxByOrNull { it.name }
                if (newest != null) return newest
            }
        }
    }
    // 2. ANDROID_NDK_HOME env var
    System.getenv("ANDROID_NDK_HOME")?.let { path ->
        val f = File(path)
        if (f.isDirectory) return f
    }
    // 3. ANDROID_NDK env var (older convention)
    System.getenv("ANDROID_NDK")?.let { path ->
        val f = File(path)
        if (f.isDirectory) return f
    }
    error(
        "Cannot find Android NDK.  Set ANDROID_NDK_HOME or add ndk.dir / sdk.dir to local.properties."
    )
}

// ---------------------------------------------------------------------------
// Resolve a usable bash executable
// ---------------------------------------------------------------------------
fun resolveBash(): String {
    // Check well-known locations first (MSYS2, Git Bash)
    val candidates = listOf(
        "C:/msys64/usr/bin/bash.exe",
        "C:/Program Files/Git/bin/bash.exe",
        "C:/Program Files (x86)/Git/bin/bash.exe",
    )
    for (c in candidates) {
        if (File(c).exists()) return c
    }
    // Fallback to PATH
    return "bash"
}

// ---------------------------------------------------------------------------
// Task: buildLibsodium
// ---------------------------------------------------------------------------
tasks.register("buildLibsodium") {
    group = "build setup"
    description = "Downloads and cross-compiles libsodium for Android arm64-v8a if not already built."

    // Gradle UP-TO-DATE tracking: if the output .a exists, skip entirely.
    outputs.file(sodiumLib)

    doLast {
        if (sodiumLib.exists()) {
            logger.lifecycle("✅ libsodium already built at ${sodiumLib.absolutePath} — skipping.")
            return@doLast
        }

        logger.lifecycle("🔨 libsodium.a not found — building from source...")

        val ndkDir = resolveNdkDir()
        logger.lifecycle("   NDK: ${ndkDir.absolutePath}")

        // ── Step 1: Prepare work directory ──────────────────────────────
        sodiumWorkDir.mkdirs()

        val tarball = File(sodiumWorkDir, tarballName)
        val srcDir  = File(sodiumWorkDir, "libsodium-stable")

        // ── Step 2: Download tarball (if not cached) ────────────────────
        if (!tarball.exists()) {
            logger.lifecycle("   Downloading $tarballUrl ...")
            java.net.URI(tarballUrl).toURL().openStream().use { input ->
                tarball.outputStream().use { output -> input.copyTo(output) }
            }
            logger.lifecycle("   Downloaded ${tarball.length() / 1024} KB")
        } else {
            logger.lifecycle("   Using cached tarball: ${tarball.absolutePath}")
        }

        // ── Step 3: Extract tarball ─────────────────────────────────────
        if (!srcDir.exists()) {
            logger.lifecycle("   Extracting tarball...")
            exec {
                workingDir = sodiumWorkDir
                // Use tar (available on Windows 10+ natively, and via MSYS2/Git)
                commandLine("tar", "xzf", tarball.absolutePath, "-C", sodiumWorkDir.absolutePath)
            }
            // The tarball extracts to libsodium-stable/
            if (!srcDir.exists()) {
                // Fallback: maybe it extracted to libsodium-$version/
                val alt = File(sodiumWorkDir, "libsodium-$sodiumVersion")
                if (alt.exists()) {
                    alt.renameTo(srcDir)
                } else {
                    error("Tarball did not extract to expected directory. Contents: ${sodiumWorkDir.listFiles()?.map { it.name }}")
                }
            }
            logger.lifecycle("   Extracted to: ${srcDir.absolutePath}")
        }

        // ── Step 4: Cross-compile for arm64-v8a ─────────────────────────
        logger.lifecycle("   Cross-compiling libsodium for arm64-v8a...")

        val bashExe = resolveBash()
        logger.lifecycle("   Using bash: $bashExe")

        // Convert Windows paths to true MSYS Unix-style paths to prevent PATH splitting on the drive colon
        fun toMsysPath(f: File): String {
            var p = f.absolutePath.replace("\\", "/")
            if (p.length > 1 && p[1] == ':') {
                p = "/" + p[0].lowercaseChar() + p.substring(2)
            }
            return p
        }

        val ndkUnixPath = toMsysPath(ndkDir)

        val osName = when {
            org.gradle.internal.os.OperatingSystem.current().isWindows -> "windows-x86_64"
            org.gradle.internal.os.OperatingSystem.current().isMacOsX -> "darwin-x86_64"
            else -> "linux-x86_64"
        }

        // Build using libsodium's own dist-build script
        exec {
            workingDir = srcDir
            val script = """
                export PATH="/usr/bin:/bin:${'$'}PATH"
                export ANDROID_NDK_HOME='$ndkUnixPath'
                export NDK_PLATFORM=android-26
                export LIBSODIUM_FULL_BUILD=1
                sed -i 's|export TOOLCHAIN_OS_DIR=.*|export TOOLCHAIN_OS_DIR="${osName}/"|g' dist-build/android-build.sh
                ./dist-build/android-armv8-a.sh
            """.trimIndent().replace("\r", "")
            
            commandLine(bashExe, "-c", script)
        }

        // The build script puts output in libsodium-android-armv8-a+crypto/
        val builtDir = File(srcDir, "libsodium-android-armv8-a+crypto")
        val builtLib = File(builtDir, "lib/libsodium.a")
        val builtInclude = File(builtDir, "include")

        if (!builtLib.exists()) {
            error("libsodium build succeeded but libsodium.a not found at ${builtLib.absolutePath}")
        }

        // ── Step 5: Copy results into third_party/libsodium/ ────────────
        logger.lifecycle("   Copying build output to ${sodiumOutputDir.absolutePath} ...")

        // Copy library
        val targetLibDir = File(sodiumOutputDir, "lib/arm64-v8a")
        targetLibDir.mkdirs()
        builtLib.copyTo(File(targetLibDir, "libsodium.a"), overwrite = true)

        // Copy headers (only if they don't already exist or are outdated)
        val targetIncludeDir = File(sodiumOutputDir, "include")
        if (!File(targetIncludeDir, "sodium.h").exists()) {
            builtInclude.copyRecursively(targetIncludeDir, overwrite = true)
            logger.lifecycle("   Copied headers to ${targetIncludeDir.absolutePath}")
        }

        logger.lifecycle("✅ libsodium built and installed successfully!")
        logger.lifecycle("   Library: ${File(targetLibDir, "libsodium.a").absolutePath}")
    }
}

// ---------------------------------------------------------------------------
// Wire: buildLibsodium must run before any native (CMake) build task
// ---------------------------------------------------------------------------
tasks.configureEach {
    if (name.contains("externalNativeBuild", ignoreCase = true) ||
        name.contains("configureCMake", ignoreCase = true) ||
        name.contains("buildCMake", ignoreCase = true)) {
        dependsOn("buildLibsodium")
    }
}
