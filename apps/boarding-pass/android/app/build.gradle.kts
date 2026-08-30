plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.compose)
}

android {
    namespace = "com.pebblewatchapps.boardingpass"
    // PebbleKit 2 requires callers to compile against API 37 or later.
    compileSdk = 37

    defaultConfig {
        applicationId = "com.pebblewatchapps.boardingpass"
        minSdk = 26
        // Deliberately one behind compileSdk: the app is sideloaded, so there
        // is nothing to gain from opting in to a newer platform's runtime
        // behaviour changes before they have been tried on a real phone.
        targetSdk = 36
        versionCode = 1
        versionName = "1.0"
    }

    buildTypes {
        debug {
            // The watchapp's package.json lists this suffixed id too, so a
            // debug build can talk to the watch without uninstalling the
            // release one.
            applicationIdSuffix = ".debug"
        }
        release {
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    buildFeatures {
        compose = true
    }
}

dependencies {
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.lifecycle.runtime.ktx)
    implementation(libs.androidx.lifecycle.viewmodel.compose)
    implementation(libs.androidx.activity.compose)
    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.ui)
    implementation(libs.androidx.ui.graphics)
    implementation(libs.androidx.ui.tooling.preview)
    implementation(libs.androidx.material3)
    implementation(libs.pebblekit)
    implementation(libs.zxing.core)

    debugImplementation(libs.androidx.ui.tooling)

    testImplementation(libs.junit)
    testImplementation(libs.zxing.core)
}
