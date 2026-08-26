import org.gradle.api.publish.maven.MavenPublication
import org.gradle.api.tasks.Copy

val versionName = "1.0.0"
val jdkVersion = 21

val releaseAarName = "com.caijunlin.vulkan-${versionName}.aar"
val releaseAarDir = layout.buildDirectory.dir("release-assets")

plugins {
    alias(libs.plugins.android.library)
    alias(libs.plugins.maven.publish)
}

kotlin {
    jvmToolchain(jdkVersion)
}

android {
    namespace = "com.caijunlin.vulkan"
    version = versionName

    compileSdk {
        version = release(libs.versions.compileSdk.get().toInt())
    }

    defaultConfig {
        minSdk = libs.versions.minSdk.get().toInt()
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        consumerProguardFiles("consumer-rules.pro")

        @Suppress("UnstableApiUsage")
        externalNativeBuild {
            cmake {
                cppFlags.add("-std=c++17")
                abiFilters.addAll(listOf("arm64-v8a"))
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.toVersion(jdkVersion)
        targetCompatibility = JavaVersion.toVersion(jdkVersion)
    }

    publishing {
        singleVariant("release") {
            // withSourcesJar()
        }
    }

    buildFeatures {
        buildConfig = true
    }
}

dependencies {
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.appcompat)
    implementation(libs.material)
    implementation(libs.libvlc)
    testImplementation(libs.junit)
    androidTestImplementation(libs.androidx.junit)
    androidTestImplementation(libs.androidx.espresso.core)
}

publishing {
    publications {
        create<MavenPublication>("release") {
            groupId = "com.github.caijunlin"
            artifactId = "vlc-decoder"
            version = versionName
            afterEvaluate {
                from(components["release"])
            }
        }
    }
}

tasks.register<Copy>("prepareReleaseAar") {
    description = ""
    group = "publishing"
    dependsOn("assembleRelease")

    from(layout.buildDirectory.file("outputs/aar/library-release.aar"))
    into(releaseAarDir)
    rename { releaseAarName }
}

tasks.register("createRelease") {
    description = ""
    group = "publishing"
    dependsOn("prepareReleaseAar")

    doLast {
        val aarFile = releaseAarDir.get().file(releaseAarName).asFile

        val checkProcess = ProcessBuilder("gh", "release", "view", versionName)
            .redirectOutput(ProcessBuilder.Redirect.DISCARD)
            .redirectError(ProcessBuilder.Redirect.DISCARD)
            .start()

        if (checkProcess.waitFor() == 0) {
            println("Release [$versionName] exists, uploading asset...")
            ProcessBuilder(
                "gh",
                "release",
                "upload",
                versionName,
                aarFile.absolutePath,
                "--clobber"
            ).inheritIO().start().waitFor()
            return@doLast
        }

        println("Release: $versionName ...")
        ProcessBuilder(
            "gh",
            "release",
            "create",
            versionName,
            aarFile.absolutePath,
            "--title",
            versionName,
            "--generate-notes"
        ).inheritIO().start().waitFor()

        println("Success!")
    }
}