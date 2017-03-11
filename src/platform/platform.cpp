//-----------------------------------------------------------------------------
// Platform-dependent functionality.
//
// Copyright 2017 whitequark
//-----------------------------------------------------------------------------
#if defined(__APPLE__)
// Include Apple headers before solvespace.h to avoid identifier clashes.
#   include <CoreFoundation/CFString.h>
#   include <CoreFoundation/CFURL.h>
#   include <CoreFoundation/CFBundle.h>
#endif
#include "solvespace.h"
#include "config.h"
#if defined(WIN32)
// Conversely, include Microsoft headers after solvespace.h to avoid clashes.
#   include <windows.h>
#else
#   include <unistd.h>
#   include <sys/stat.h>
#endif

namespace SolveSpace {
namespace Platform {

//-----------------------------------------------------------------------------
// UTF-8 ⟷ UTF-16 conversion, on Windows.
//-----------------------------------------------------------------------------

#if defined(WIN32)

std::string Narrow(const wchar_t *in)
{
    std::string out;
    DWORD len = WideCharToMultiByte(CP_UTF8, 0, in, -1, NULL, 0, NULL, NULL);
    out.resize(len - 1);
    ssassert(WideCharToMultiByte(CP_UTF8, 0, in, -1, &out[0], len, NULL, NULL),
             "Invalid UTF-16");
    return out;
}

std::string Narrow(const std::wstring &in)
{
    if(in == L"") return "";

    std::string out;
    out.resize(WideCharToMultiByte(CP_UTF8, 0, &in[0], (int)in.length(),
                                   NULL, 0, NULL, NULL));
    ssassert(WideCharToMultiByte(CP_UTF8, 0, &in[0], (int)in.length(),
                                 &out[0], (int)out.length(), NULL, NULL),
             "Invalid UTF-16");
    return out;
}

std::wstring Widen(const char *in)
{
    std::wstring out;
    DWORD len = MultiByteToWideChar(CP_UTF8, 0, in, -1, NULL, 0);
    out.resize(len - 1);
    ssassert(MultiByteToWideChar(CP_UTF8, 0, in, -1, &out[0], len),
             "Invalid UTF-8");
    return out;
}

std::wstring Widen(const std::string &in)
{
    if(in == "") return L"";

    std::wstring out;
    out.resize(MultiByteToWideChar(CP_UTF8, 0, &in[0], (int)in.length(), NULL, 0));
    ssassert(MultiByteToWideChar(CP_UTF8, 0, &in[0], (int)in.length(),
                                 &out[0], (int)out.length()),
             "Invalid UTF-8");
    return out;
}

#endif

//-----------------------------------------------------------------------------
// Path utility functions.
//-----------------------------------------------------------------------------

static std::vector<std::string> Split(const std::string &joined, char separator) {
    std::vector<std::string> parts;

    size_t oldpos = 0, pos = 0;
    while(true) {
        oldpos = pos;
        pos = joined.find(separator, pos);
        if(pos == std::string::npos) break;
        parts.push_back(joined.substr(oldpos, pos - oldpos));
        pos += 1;
    }

    if(oldpos != joined.length() - 1) {
        parts.push_back(joined.substr(oldpos));
    }

    return parts;
}

static std::string Concat(const std::vector<std::string> &parts, char separator) {
    std::string joined;

    bool first = true;
    for(auto &part : parts) {
        if(!first) joined += separator;
        joined += part;
        first = false;
    }

    return joined;
}

//-----------------------------------------------------------------------------
// Path manipulation.
//-----------------------------------------------------------------------------

#if defined(WIN32)
const char SEPARATOR = '\\';
#else
const char SEPARATOR = '/';
#endif

Path Path::From(std::string raw) {
    Path path = { raw };
    return path;
}

Path Path::CurrentDirectory() {
#if defined(WIN32)
    // On Windows, OpenFile needs an absolute UNC path proper, so get that.
    std::wstring rawW;
    rawW.resize(GetCurrentDirectoryW(0, NULL));
    DWORD length = GetCurrentDirectoryW((int)rawW.length(), &rawW[0]);
    ssassert(length > 0 && length == rawW.length() - 1, "Cannot get current directory");
    rawW.resize(length);
    return From(Narrow(rawW));
#else
    char *raw = getcwd(NULL, 0);
    ssassert(raw != NULL, "Cannot get current directory");
    Path path = From(raw);
    free(raw);
    return path;
#endif
}

std::string Path::FileName() const {
    std::string fileName = raw;
    size_t slash = fileName.rfind(SEPARATOR);
    if(slash != std::string::npos) {
        fileName = fileName.substr(slash + 1);
    }
    return fileName;
}

std::string Path::FileStem() const {
    std::string baseName = FileName();
    size_t dot = baseName.rfind('.');
    if(dot != std::string::npos) {
        baseName = baseName.substr(0, dot);
    }
    return baseName;
}

std::string Path::Extension() const {
    size_t dot = raw.rfind('.');
    if(dot != std::string::npos) {
        return raw.substr(dot + 1);
    }
    return "";
}

bool Path::HasExtension(std::string theirExt) const {
    std::string ourExt = Extension();
    std::transform(ourExt.begin(),   ourExt.end(),   ourExt.begin(),   ::tolower);
    std::transform(theirExt.begin(), theirExt.end(), theirExt.begin(), ::tolower);
    return ourExt == theirExt;
}

Path Path::WithExtension(std::string ext) const {
    Path withExt = *this;
    size_t dot = withExt.raw.rfind('.');
    if(dot != std::string::npos) {
        withExt.raw.erase(dot);
    }
    withExt.raw += ".";
    withExt.raw += ext;
    return withExt;
}

static void FindPrefix(const std::string &raw, size_t *pos) {
    *pos = std::string::npos;
#if defined(WIN32)
    if(raw.size() >= 7 && raw[2] == '?' && raw[3] == '\\' &&
            isalpha(raw[4]) && raw[5] == ':' && raw[6] == '\\') {
        *pos = 7;
    } else if(raw.size() >= 3 && isalpha(raw[0]) && raw[1] == ':' && raw[2] == '\\') {
        *pos = 3;
    } else if(raw.size() >= 2 && raw[0] == '\\' && raw[1] == '\\') {
        size_t slashAt = raw.find('\\', 2);
        if(slashAt != std::string::npos) {
            *pos = raw.find('\\', slashAt + 1);
        }
    }
#else
    if(raw.size() >= 1 && raw[0] == '/') {
        *pos = 1;
    }
#endif
}

bool Path::IsAbsolute() const {
    size_t pos;
    FindPrefix(raw, &pos);
    return pos != std::string::npos;
}

// Removes one component from the end of the path.
// Returns an empty path if the path consists only of a root.
Path Path::Parent() const {
    Path parent = { raw };
    if(!parent.raw.empty() && parent.raw.back() == SEPARATOR) {
        parent.raw.pop_back();
    }
    size_t slash = parent.raw.rfind(SEPARATOR);
    if(slash != std::string::npos) {
        parent.raw = parent.raw.substr(0, slash + 1);
    } else {
        parent.raw.clear();
    }
    if(IsAbsolute() && !parent.IsAbsolute()) {
        return From("");
    }
    return parent;
}

// Concatenates a component to this path.
// Returns an empty path if this path or the component is empty.
Path Path::Join(const std::string &component) const {
    ssassert(component.find(SEPARATOR) == std::string::npos,
             "Use the Path::Join(const Path &) overload to append an entire path");
    return Join(Path::From(component));
}

// Concatenates a relative path to this path.
// Returns an empty path if either path is empty, or the other path is absolute.
Path Path::Join(const Path &other) const {
    if(IsEmpty() || other.IsEmpty() || other.IsAbsolute()) {
        return From("");
    }

    Path joined = { raw };
    if(joined.raw.back() != SEPARATOR) {
        joined.raw += SEPARATOR;
    }
    joined.raw += other.raw;
    return joined;
}

// Expands the "." and ".." components in this path.
// On Windows, additionally prepends the UNC prefix to absolute paths without one.
// Returns an empty path if a ".." component would escape from the root.
Path Path::Expand(bool fromCurrentDirectory) const {
    Path source;
    Path expanded;

    if(fromCurrentDirectory && !IsAbsolute()) {
        source = CurrentDirectory().Join(*this);
    } else {
        source = *this;
    }

    size_t splitAt;
    FindPrefix(source.raw, &splitAt);
    if(splitAt != std::string::npos) {
        expanded.raw = source.raw.substr(0, splitAt);
    } else {
        splitAt = 0;
    }

    std::vector<std::string> expandedComponents;
    for(std::string component : Split(source.raw.substr(splitAt), SEPARATOR)) {
        if(component == ".") {
            // skip
        } else if(component == "..") {
            if(!expandedComponents.empty()) {
                expandedComponents.pop_back();
            } else {
                return From("");
            }
        } else if(!component.empty()) {
            expandedComponents.push_back(component);
        }
    }

    if(expanded.IsEmpty()) {
        if(expandedComponents.empty()) {
            expandedComponents.push_back(".");
        }
        expanded = From(Concat(expandedComponents, SEPARATOR));
    } else if(!expandedComponents.empty()) {
        expanded = expanded.Join(From(Concat(expandedComponents, SEPARATOR)));
    }

#if defined(WIN32)
    if(expanded.IsAbsolute() && expanded.raw.substr(0, 2) != "\\\\") {
        expanded.raw = "\\\\?\\" + expanded.raw;
    }
#endif

    return expanded;
}

static std::string FilesystemNormalize(const std::string &str) {
#if defined(WIN32)
    std::wstring strW = Widen(str);
    std::transform(strW.begin(), strW.end(), strW.begin(), towlower);
    return Narrow(strW);
#elif defined(__APPLE__)
    CFMutableStringRef cfStr =
        CFStringCreateMutableCopy(NULL, 0,
            CFStringCreateWithBytesNoCopy(NULL, (const UInt8*)str.data(), str.size(),
                kCFStringEncodingUTF8, /*isExternalRepresentation=*/false, kCFAllocatorNull));
    CFStringLowercase(cfStr, NULL);
    std::string normalizedStr;
    normalizedStr.resize(CFStringGetMaximumSizeOfFileSystemRepresentation(cfStr));
    CFStringGetFileSystemRepresentation(cfStr, &normalizedStr[0], normalizedStr.size());
    return normalizedStr;
#else
    return str;
#endif
}

bool Path::Equals(const Path &other) const {
    return FilesystemNormalize(raw) == FilesystemNormalize(other.raw);
}

// Returns a relative path from a given base path.
// Returns an empty path if any of the paths is not absolute, or
// if they belong to different roots, or
// if they cannot be expanded.
Path Path::RelativeTo(const Path &base) const {
    Path expanded = Expand();
    Path baseExpanded = base.Expand();
    if(!(expanded.IsAbsolute() && baseExpanded.IsAbsolute())){
        return From("");
    }

    size_t splitAt;
    FindPrefix(expanded.raw, &splitAt);
    size_t baseSplitAt;
    FindPrefix(baseExpanded.raw, &baseSplitAt);
    if(FilesystemNormalize(expanded.raw.substr(0, splitAt)) !=
            FilesystemNormalize(baseExpanded.raw.substr(0, splitAt))) {
        return From("");
    }

    std::vector<std::string> components =
        Split(expanded.raw.substr(splitAt), SEPARATOR);
    std::vector<std::string> baseComponents =
        Split(baseExpanded.raw.substr(baseSplitAt), SEPARATOR);
    size_t common;
    for(common = 0; common < baseComponents.size() &&
                    common < components.size(); common++) {
        if(FilesystemNormalize(baseComponents[common]) !=
                FilesystemNormalize(components[common])) {
            break;
        }
    }

    std::vector<std::string> resultComponents;
    for(size_t i = common; i < baseComponents.size(); i++) {
        resultComponents.push_back("..");
    }
    resultComponents.insert(resultComponents.end(),
                            components.begin() + common, components.end());
    if(resultComponents.empty()) {
        resultComponents.push_back(".");
    }
    return From(Concat(resultComponents, SEPARATOR));
}

Path Path::FromPortable(const std::string &repr) {
    return From(Concat(Split(repr, '/'), SEPARATOR));
}

std::string Path::ToPortable() const {
    ssassert(!IsAbsolute(), "absolute paths cannot be made portable");

    return Concat(Split(raw, SEPARATOR), '/');
}

//-----------------------------------------------------------------------------
// File manipulation.
//-----------------------------------------------------------------------------

FILE *OpenFile(const Platform::Path &filename, const char *mode) {
    ssassert(filename.raw.length() == strlen(filename.raw.c_str()),
             "Unexpected null byte in middle of a path");
#if defined(WIN32)
    return _wfopen(Widen(filename.Expand().raw).c_str(), Widen(mode).c_str());
#else
    return fopen(filename.raw.c_str(), mode);
#endif
}

void RemoveFile(const Platform::Path &filename) {
    ssassert(filename.raw.length() == strlen(filename.raw.c_str()),
             "Unexpected null byte in middle of a path");
#if defined(WIN32)
    _wremove(Widen(filename.Expand().raw).c_str());
#else
    remove(filename.raw.c_str());
#endif
}

bool ReadFile(const Platform::Path &filename, std::string *data) {
    FILE *f = OpenFile(filename, "rb");
    if(f == NULL) return false;

    fseek(f, 0, SEEK_END);
    data->resize(ftell(f));
    fseek(f, 0, SEEK_SET);
    fread(&(*data)[0], 1, data->size(), f);
    fclose(f);

    return true;
}

bool WriteFile(const Platform::Path &filename, const std::string &data) {
    FILE *f = OpenFile(filename, "wb");
    if(f == NULL) return false;

    fwrite(&data[0], 1, data.size(), f);
    fclose(f);

    return true;
}

//-----------------------------------------------------------------------------
// Loading resources, on Windows.
//-----------------------------------------------------------------------------

#if defined(WIN32)

const void *LoadResource(const std::string &name, size_t *size) {
    HRSRC hres = FindResourceW(NULL, Widen(name).c_str(), RT_RCDATA);
    ssassert(hres != NULL, "Cannot find resource");
    HGLOBAL res = ::LoadResource(NULL, hres);
    ssassert(res != NULL, "Cannot load resource");

    *size = SizeofResource(NULL, hres);
    return LockResource(res);
}

#endif

//-----------------------------------------------------------------------------
// Loading resources, on *nix.
//-----------------------------------------------------------------------------

#if !defined(WIN32)

#    if defined(__linux__)
static const char *selfSymlink = "/proc/self/exe";
#    elif defined(__NetBSD__)
static const char *selfSymlink = "/proc/curproc/exe"
#    elif defined(__OpenBSD__) || defined(__FreeBSD__)
static const char *selfSymlink = "/proc/curproc/file";
#    else
static const char *selfSymlink = "";
#    endif

static Platform::Path FindLocalResourceDir() {
#if defined(__APPLE__)
    Platform::Path resourceDir;
    CFURLRef cfUrl = CFBundleCopyResourcesDirectoryURL(CFBundleGetMainBundle());
    CFStringRef cfPath = CFURLCopyFileSystemPath(cfUrl, kCFURLPOSIXPathStyle);
    resourceDir.raw.resize(CFStringGetLength(cfPath) + 1); // reserve space for NUL
    ssassert(CFStringGetCString(cfPath, &resourceDir.raw[0], resourceDir.raw.size(),
                                kCFStringEncodingUTF8),
             "Cannot convert CFString to C string");
    resourceDir.raw.resize(resourceDir.raw.size() - 1);
    CFRelease(cfUrl);
    CFRelease(cfPath);
    return resourceDir;
#else
    // Find out the path to the running binary.
    Platform::Path selfPath;
    char *expandedSelfPath = realpath(selfSymlink, NULL);
    if(expandedSelfPath != NULL) {
        selfPath = Path::From(expandedSelfPath);
    }
    free(expandedSelfPath);

    Platform::Path resourceDir;
    if(selfPath.IsEmpty()) {
        // We don't know how to find the local resource directory on this platform,
        // so use the global one (by returning an empty string).
        return Path::From(UNIX_DATADIR);
    } else {
        resourceDir = selfPath.Parent().Parent().Join("res");
    }

    struct stat st;
    if(stat(resourceDir.raw.c_str(), &st) != -1) {
        // An executable-adjacent resource directory exists, good.
        return resourceDir;
    }

    // No executable-adjacent resource directory; use the one from compile-time prefix.
    return Path::From(UNIX_DATADIR);
#endif
}

const void *LoadResource(const std::string &name, size_t *size) {
    static Platform::Path resourceDir;
    static std::map<std::string, std::string> cache;

    if(resourceDir.IsEmpty()) {
        resourceDir = FindLocalResourceDir();
    }

    auto it = cache.find(name);
    if(it == cache.end()) {
        ssassert(ReadFile(resourceDir.Join(Path::From(name)), &cache[name]),
                 "Cannot read resource");
        it = cache.find(name);
    }

    const std::string &content = (*it).second;
    *size = content.size();
    return (const void*)content.data();
}

#endif

}
}