// Script-only renderer profile: no file syscalls/grants exist in this port yet.
// Return operation failures, never expose host paths or pretend writes succeed.
#include "config.h"
#include <wtf/FileSystem.h>
#include <wtf/FileHandle.h>
#include <wtf/MappedFileData.h>
#include <wtf/Vector.h>
#include <wtf/WallTime.h>
#include <wtf/text/CString.h>
#include <wtf/text/WTFString.h>
#include <wtf/text/MakeString.h>
#include <wtf/text/StringBuilder.h>
namespace WTF::FileSystemImpl {
FileHandle openFile(const String&,FileOpenMode,FileAccessPermission,OptionSet<FileLockMode>,bool){return { };}
std::pair<String,FileHandle> openTemporaryFile(StringView,StringView,const String&){return {String(),FileHandle()};}
std::pair<FileHandle,CString> createTemporaryFileInDirectory(const String&,const String&){return {FileHandle(),CString()};}
FileHandle createDumpFile(StringView,StringView,StringView){return { };}
String createTemporaryDirectory(){return { };}
bool fileExists(const String&){return false;}
bool deleteFile(const String&){return false;}
bool deleteEmptyDirectory(const String&){return false;}
bool moveFile(const String&,const String&){return false;}
bool makeAllDirectories(const String&){return false;}
bool deleteNonEmptyDirectory(const String&){return false;}
bool createSymbolicLink(const String&,const String&){return false;}
bool hardLink(const String&,const String&){return false;}
bool hardLinkOrCopyFile(const String&,const String&){return false;}
bool copyFile(const String&,const String&){return false;}
bool updateFileModificationTime(const String&){return false;}
bool isHiddenFile(const String&){return false;}
std::optional<uint64_t> fileSize(const String&){return { };}
std::optional<uint64_t> directorySize(const String&){return { };}
std::optional<uint64_t> volumeFreeSpace(const String&){return { };}
std::optional<uint64_t> volumeCapacity(const String&){return { };}
std::optional<uint64_t> hardLinkCount(const String&){return { };}
std::optional<uint32_t> volumeFileBlockSize(const String&){return { };}
std::optional<int32_t> getFileDeviceId(const String&){return { };}
std::optional<WallTime> fileModificationTime(const String&){return { };}
std::optional<WallTime> fileCreationTime(const String&){return { };}
std::optional<FileType> fileType(const String&){return { };}
std::optional<FileType> fileTypeFollowingSymlinks(const String&){return { };}
Vector<String> listDirectory(const String&){return { };}
void traverseDirectory(const String&,NOESCAPE const Function<void(const String&,FileType)>&){ }
bool fileIDsAreEqual(std::optional<PlatformFileID> a,std::optional<PlatformFileID> b){return a && b && *a==*b;}
String realPath(const String&){return { };}
CString fileSystemRepresentation(const String& path){return path.contains(Latin1Character('\0'))?CString():path.utf8();}
String stringFromFileSystemRepresentation(const char* path){return path?String::fromUTF8(path):String();}
String pathByAppendingComponent(StringView path,StringView component){
    if(path.isEmpty())return component.toString();
    return path.endsWith('/')?makeString(path,component):makeString(path,'/',component);
}
String pathByAppendingComponents(StringView path,std::span<const StringView> components){
    String result=path.toString();for(auto part:components)result=pathByAppendingComponent(result,part);return result;
}
String pathFileName(const String& path){return lastComponentOfPathIgnoringTrailingSlash(path);}
String parentPath(const String& path){
    auto slash=path.reverseFind('/');if(slash==notFound)return { };return path.substring(0,slash?slash:1);
}
// FileHandle can only be invalid in this profile; even adopt() rejects foreign
// descriptor values. Keep failed reads/writes distinct from successful EOF/0.
std::optional<uint64_t> FileHandle::read(std::span<uint8_t>){return { };}
std::optional<uint64_t> FileHandle::write(std::span<const uint8_t>){return { };}
std::optional<uint64_t> FileHandle::seek(int64_t,FileSeekOrigin){return { };}
std::optional<uint64_t> FileHandle::size(){return { };}
std::optional<PlatformFileID> FileHandle::id(){return { };}
std::optional<MappedFileData> FileHandle::map(MappedFileMode,FileOpenMode){return { };}
bool FileHandle::truncate(int64_t){return false;}
bool FileHandle::flush(){return false;}
void FileHandle::close(){m_handle=std::nullopt;}
MappedFileData::~MappedFileData()=default;
} // namespace WTF::FileSystemImpl
