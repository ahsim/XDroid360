// libstfsextract.so: the JNI side of the launcher's StfsExtractor (recipe format v2, "STFS
// extraction"), loaded in-process so that no child process ever needs the SAF URI:
//
//   final class StfsExtractor {
//       static { System.loadLibrary("stfsextract"); }
//       interface Progress { boolean onProgress(long done, long total); } // false = cancel
//       static native String nativeProbe(int fd);
//       static native int nativeExtract(int fd, String outDir, Progress progress);
//   }
//
// The fd is the launcher's (ParcelFileDescriptor.getFd()): read with pread only and never
// closed here. outDir is the instance's shared folder (${GAME}); each package goes where
// SoD's runtime looks for its content type (stfs::TargetFolder).
#include <jni.h>

#include <android/log.h>

#include <string>

#include "stfs_reader.h"

#define LOG_TAG "stfsextract"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace
{
    std::string ToUtf8(JNIEnv* env, jstring s)
    {
        if (!s)
            return {};
        const char* chars = env->GetStringUTFChars(s, nullptr);
        std::string out = chars ? chars : "";
        if (chars)
            env->ReleaseStringUTFChars(s, chars);
        return out;
    }
}

// The package's header as JSON ({"title_id":"584111E8","content_type":"0x000D0000",
// "content_id":..., "media_id":..., "magic":"LIVE", "display_name":"State of Decay",
// "size":<bytes extraction writes>}), or null if fd is not an Xbox 360 package.
extern "C" JNIEXPORT jstring JNICALL Java_com_udarmolota_xdroid360_StfsExtractor_nativeProbe(JNIEnv* env, jclass,
    jint fd)
{
    stfs::Result result;
    std::string error;
    auto package = stfs::Package::Open(fd, result, error);
    if (!package)
    {
        LOGI("probe fd %d: %s", int(fd), error.c_str());
        return nullptr;
    }
    return env->NewStringUTF(stfs::ProbeJson(*package).c_str());
}

// Extracts the package to its folder under outDir. 0 on success, also for a package of a
// content type the game doesn't install (a save, ...: passed over, nothing written);
// 1 source_unreadable, 2 source_not_seekable, 3 extract_failed, 4 not_enough_space,
// 5 cancelled (the progress callback returned false, or threw).
extern "C" JNIEXPORT jint JNICALL Java_com_udarmolota_xdroid360_StfsExtractor_nativeExtract(JNIEnv* env, jclass,
    jint fd, jstring outDir, jobject progress)
{
    stfs::Result result;
    std::string error;
    auto package = stfs::Package::Open(fd, result, error);
    if (!package)
    {
        LOGW("extract fd %d: %s", int(fd), error.c_str());
        return jint(result);
    }
    const std::string folder = stfs::TargetFolder(package->header, ToUtf8(env, outDir));
    if (folder.empty())
    {
        LOGI("extract %s: content type %08X is passed over", package->header.contentId.c_str(),
            package->header.contentType);
        return jint(stfs::Result::Ok);
    }
    if ((result = package->ReadTable(error)) != stfs::Result::Ok)
    {
        LOGW("extract %s: %s", package->header.contentId.c_str(), error.c_str());
        return jint(result);
    }

    jmethodID onProgress = nullptr;
    if (progress)
    {
        jclass cls = env->GetObjectClass(progress);
        onProgress = env->GetMethodID(cls, "onProgress", "(JJ)Z");
        env->DeleteLocalRef(cls);
        if (!onProgress)
        {
            env->ExceptionClear();
            return jint(stfs::Result::ExtractFailed);
        }
    }
    auto report = [&](uint64_t done, uint64_t total) {
        if (!onProgress)
            return true;
        const jboolean go = env->CallBooleanMethod(progress, onProgress, jlong(done), jlong(total));
        if (env->ExceptionCheck())
        {
            env->ExceptionClear();
            return false;
        }
        return go == JNI_TRUE;
    };

    result = package->Extract(folder, report, error);
    if (result == stfs::Result::Ok && package->header.contentType == stfs::kContentMarketplace)
    {
        // <content ID>.name beside the DLC's folder: the name the game lists it by.
        const std::string parent = folder.substr(0, folder.find_last_of('/'));
        if (!stfs::WriteDisplayName(package->header, parent))
        {
            error = "can't write the display name";
            result = stfs::Result::ExtractFailed;
        }
    }
    if (result == stfs::Result::Ok)
        LOGI("extracted %s (%08X, %zu files) to %s", package->header.contentId.c_str(),
            package->header.contentType, package->files.size(), folder.c_str());
    else
        LOGW("extract %s: %s (%d)", package->header.contentId.c_str(), error.c_str(), int(result));
    return jint(result);
}
