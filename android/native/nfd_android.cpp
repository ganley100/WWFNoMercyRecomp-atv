// nativefiledialog-extended backend for Android.
//
// Desktop builds open GTK/Win32 file dialogs; Android has none, so each dialog
// calls into GameActivity (Java), which shows the system document picker and
// copies the chosen file into the app's private storage. The engine then gets a
// normal file path, exactly as on desktop. Folder pickers return the app's own
// storage folder, and save dialogs return a path inside it.

#include <jni.h>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "SDL.h"
#include "SDL_system.h"
#include "nfd.h"

namespace {

std::string g_error;

void set_error(const char* msg) { g_error = msg ? msg : ""; }

char* dup_path(const std::string& s) {
    char* out = static_cast<char*>(std::malloc(s.size() + 1));
    std::memcpy(out, s.c_str(), s.size() + 1);
    return out;
}

// Turns "z64,n64,v64" style filter specs into one comma-separated list.
std::string join_filters(const nfdnfilteritem_t* filters, nfdfiltersize_t count) {
    std::string out;
    for (nfdfiltersize_t i = 0; i < count; i++) {
        if (filters[i].spec == nullptr) continue;
        if (!out.empty()) out += ',';
        out += filters[i].spec;
    }
    return out;
}

jclass activity_class(JNIEnv* env) {
    jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (activity == nullptr) return nullptr;
    jclass cls = env->GetObjectClass(activity);
    env->DeleteLocalRef(activity);
    return cls;
}

// Calls GameActivity.pickFiles(String extensions, boolean multiple): blocks until
// the user picks or cancels, returns local paths (empty array on cancel).
nfdresult_t pick_files(const std::string& extensions, bool multiple, std::vector<std::string>& out) {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    if (env == nullptr) { set_error("No JNI environment"); return NFD_ERROR; }
    jclass cls = activity_class(env);
    if (cls == nullptr) { set_error("No Android activity"); return NFD_ERROR; }
    jmethodID mid = env->GetStaticMethodID(cls, "pickFiles", "(Ljava/lang/String;Z)[Ljava/lang/String;");
    if (mid == nullptr) {
        env->ExceptionClear();
        env->DeleteLocalRef(cls);
        set_error("GameActivity.pickFiles missing");
        return NFD_ERROR;
    }
    jstring jext = env->NewStringUTF(extensions.c_str());
    auto arr = static_cast<jobjectArray>(env->CallStaticObjectMethod(cls, mid, jext, (jboolean)multiple));
    env->DeleteLocalRef(jext);
    env->DeleteLocalRef(cls);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        set_error("File picker failed");
        return NFD_ERROR;
    }
    if (arr == nullptr) return NFD_CANCEL;
    jsize n = env->GetArrayLength(arr);
    for (jsize i = 0; i < n; i++) {
        auto js = static_cast<jstring>(env->GetObjectArrayElement(arr, i));
        const char* c = env->GetStringUTFChars(js, nullptr);
        out.emplace_back(c);
        env->ReleaseStringUTFChars(js, c);
        env->DeleteLocalRef(js);
    }
    env->DeleteLocalRef(arr);
    return out.empty() ? NFD_CANCEL : NFD_OKAY;
}

std::string storage_dir() {
    const char* p = SDL_AndroidGetExternalStoragePath();
    if (p == nullptr) p = SDL_AndroidGetInternalStoragePath();
    return p ? std::string(p) : std::string(".");
}

struct PathSet {
    std::vector<std::string> paths;
};

}  // namespace

extern "C" {

nfdresult_t NFD_Init(void) { return NFD_OKAY; }
void NFD_Quit(void) {}
const char* NFD_GetError(void) { return g_error.empty() ? nullptr : g_error.c_str(); }
void NFD_ClearError(void) { g_error.clear(); }

void NFD_FreePathN(nfdnchar_t* filePath) { std::free(filePath); }
void NFD_FreePathU8(nfdu8char_t* filePath) { std::free(filePath); }

nfdresult_t NFD_OpenDialogN(nfdnchar_t** outPath, const nfdnfilteritem_t* filterList,
                            nfdfiltersize_t filterCount, const nfdnchar_t*) {
    std::vector<std::string> paths;
    nfdresult_t r = pick_files(join_filters(filterList, filterCount), false, paths);
    if (r == NFD_OKAY) *outPath = dup_path(paths[0]);
    return r;
}
nfdresult_t NFD_OpenDialogU8(nfdu8char_t** outPath, const nfdu8filteritem_t* filterList,
                             nfdfiltersize_t filterCount, const nfdu8char_t* defaultPath) {
    return NFD_OpenDialogN(outPath, filterList, filterCount, defaultPath);
}

nfdresult_t NFD_OpenDialogMultipleN(const nfdpathset_t** outPaths, const nfdnfilteritem_t* filterList,
                                    nfdfiltersize_t filterCount, const nfdnchar_t*) {
    auto* set = new PathSet();
    nfdresult_t r = pick_files(join_filters(filterList, filterCount), true, set->paths);
    if (r != NFD_OKAY) { delete set; return r; }
    *outPaths = set;
    return NFD_OKAY;
}
nfdresult_t NFD_OpenDialogMultipleU8(const nfdpathset_t** outPaths, const nfdu8filteritem_t* filterList,
                                     nfdfiltersize_t filterCount, const nfdu8char_t* defaultPath) {
    return NFD_OpenDialogMultipleN(outPaths, filterList, filterCount, defaultPath);
}

nfdresult_t NFD_SaveDialogN(nfdnchar_t** outPath, const nfdnfilteritem_t*, nfdfiltersize_t,
                            const nfdnchar_t*, const nfdnchar_t* defaultName) {
    std::string name = (defaultName && *defaultName) ? defaultName : "export";
    *outPath = dup_path(storage_dir() + "/" + name);
    return NFD_OKAY;
}
nfdresult_t NFD_SaveDialogU8(nfdu8char_t** outPath, const nfdu8filteritem_t* filterList,
                             nfdfiltersize_t filterCount, const nfdu8char_t* defaultPath,
                             const nfdu8char_t* defaultName) {
    return NFD_SaveDialogN(outPath, filterList, filterCount, defaultPath, defaultName);
}

nfdresult_t NFD_PickFolderN(nfdnchar_t** outPath, const nfdnchar_t*) {
    *outPath = dup_path(storage_dir());
    return NFD_OKAY;
}
nfdresult_t NFD_PickFolderU8(nfdu8char_t** outPath, const nfdu8char_t* defaultPath) {
    return NFD_PickFolderN(outPath, defaultPath);
}

nfdresult_t NFD_PathSet_GetCount(const nfdpathset_t* pathSet, nfdpathsetsize_t* count) {
    *count = static_cast<nfdpathsetsize_t>(static_cast<const PathSet*>(pathSet)->paths.size());
    return NFD_OKAY;
}
nfdresult_t NFD_PathSet_GetPathN(const nfdpathset_t* pathSet, nfdpathsetsize_t index, nfdnchar_t** outPath) {
    const auto* set = static_cast<const PathSet*>(pathSet);
    if (index >= set->paths.size()) { set_error("Index out of range"); return NFD_ERROR; }
    *outPath = dup_path(set->paths[index]);
    return NFD_OKAY;
}
nfdresult_t NFD_PathSet_GetPathU8(const nfdpathset_t* pathSet, nfdpathsetsize_t index, nfdu8char_t** outPath) {
    return NFD_PathSet_GetPathN(pathSet, index, outPath);
}
void NFD_PathSet_FreePathN(const nfdnchar_t* filePath) { std::free(const_cast<nfdnchar_t*>(filePath)); }
void NFD_PathSet_FreePathU8(const nfdu8char_t* filePath) { std::free(const_cast<nfdu8char_t*>(filePath)); }

nfdresult_t NFD_PathSet_GetEnum(const nfdpathset_t* pathSet, nfdpathsetenum_t* outEnumerator) {
    // ptr holds a heap cursor: {set, next index}
    auto* cursor = new std::pair<const PathSet*, size_t>(static_cast<const PathSet*>(pathSet), 0);
    outEnumerator->ptr = cursor;
    return NFD_OKAY;
}
void NFD_PathSet_FreeEnum(nfdpathsetenum_t* enumerator) {
    delete static_cast<std::pair<const PathSet*, size_t>*>(enumerator->ptr);
    enumerator->ptr = nullptr;
}
nfdresult_t NFD_PathSet_EnumNextN(nfdpathsetenum_t* enumerator, nfdnchar_t** outPath) {
    auto* cursor = static_cast<std::pair<const PathSet*, size_t>*>(enumerator->ptr);
    if (cursor->second >= cursor->first->paths.size()) { *outPath = nullptr; return NFD_OKAY; }
    *outPath = dup_path(cursor->first->paths[cursor->second++]);
    return NFD_OKAY;
}
nfdresult_t NFD_PathSet_EnumNextU8(nfdpathsetenum_t* enumerator, nfdu8char_t** outPath) {
    return NFD_PathSet_EnumNextN(enumerator, outPath);
}
void NFD_PathSet_Free(const nfdpathset_t* pathSet) { delete static_cast<const PathSet*>(pathSet); }

}  // extern "C"
