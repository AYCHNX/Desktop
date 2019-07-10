/*
 * Copyright (C) 2018 by AMCO
 * Copyright (C) 2018 by Jesús Deloya <jdeloya_ext@amco.mx>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "vfs_mac.h"
#include "fileManager.h"
#include "discoveryphase.h"

#include <sys/param.h>
#include <sys/mount.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>
#include <sys/vnode.h>
#include <sys/xattr.h>
#include <libproc.h>

#include <QtCore>
#include <thread>

#include <sys/ioctl.h>
#include <dirent.h>

#define G_PREFIX                       "org"
#define G_KAUTH_FILESEC_XATTR G_PREFIX ".apple.system.Security"
#define A_PREFIX                       "com"
#define A_KAUTH_FILESEC_XATTR A_PREFIX ".apple.system.Security"
#define XATTR_APPLE_PREFIX             "com.apple."

struct loopback {
    int case_insensitive;
};

class InternalVfsMac : public QObject
{
private:
    struct fuse* handle_;
    QString mountPath_;
    QString rootPath_;
    VfsMac::GMUserFileSystemStatus status_;
    bool shouldCheckForResource_;     // Try to handle FinderInfo/Resource Forks?
    bool isThreadSafe_;               // Is the delegate thread-safe?
    bool supportsAllocate_;           // Delegate supports preallocation of files?
    bool supportsCaseSensitiveNames_; // Delegate supports case sensitive names?
    bool supportsExchangeData_;       // Delegate supports exchange data?
    bool supportsExtendedTimes_;      // Delegate supports create and backup times?
    bool supportsSetVolumeName_;      // Delegate supports setvolname?
    bool isReadOnly_;                 // Is this mounted read-only?

public:
    explicit InternalVfsMac(QObject *parent = 0, bool isThreadSafe=false)
        : QObject(parent)
    {
        status_ = VfsMac::GMUserFileSystem_NOT_MOUNTED;
        isThreadSafe_ = isThreadSafe;
        supportsAllocate_ = false;
        supportsCaseSensitiveNames_ = true;
        supportsExchangeData_ = false;
        supportsExtendedTimes_ = true;
        supportsSetVolumeName_ = false;
        isReadOnly_ = false;
        shouldCheckForResource_=false;
    }

    struct fuse* handle () {
        return handle_;
    }

    void setHandle (struct fuse *handle) {
        handle_ = handle;
    }

    void setMountPath (QString mountPath) {
        mountPath_ = mountPath;
    }

    QString mountPath () {
        return mountPath_;
    }

    void setRootPath(QString rootPath) {
        rootPath_ = rootPath;
    }

    QString rootPath() {
        return "-omodules=threadid:subdir,subdir="+ rootPath_;
    }

    VfsMac::GMUserFileSystemStatus status()
    {
        return this->status_;
    }

    void setStatus (VfsMac::GMUserFileSystemStatus status) { this->status_ = status; }
    bool isThreadSafe () { return isThreadSafe_; }
    bool supportsAllocate () { return supportsAllocate_; };
    void setSupportsAllocate (bool val) { supportsAllocate_ = val; }
    bool supportsCaseSensitiveNames () { return supportsCaseSensitiveNames_; }
    void setSupportsCaseSensitiveNames (bool val) { supportsCaseSensitiveNames_ = val; }
    bool supportsExchangeData () { return supportsExchangeData_; }
    void setSupportsExchangeData (bool val) { supportsExchangeData_ = val; }
    bool supportsExtendedTimes () { return supportsExtendedTimes_; }
    void setSupportsExtendedTimes (bool val) { supportsExtendedTimes_ = val; }
    bool supportsSetVolumeName () { return supportsSetVolumeName_; }
    void setSupportsSetVolumeName (bool val) { supportsSetVolumeName_ = val; }
    bool shouldCheckForResource () { return shouldCheckForResource_; }
    bool isReadOnly () { return isReadOnly_; }
    void setIsReadOnly (bool val) { isReadOnly_ = val; }
    ~InternalVfsMac() {  }
};

VfsMac::VfsMac(QString rootPath, bool isThreadSafe, OCC::AccountState *accountState, QObject *parent)
    :QObject(parent)
    , internal_(new InternalVfsMac(parent, isThreadSafe))
    , accountState_(accountState)
{
    rootPath_ = rootPath;
    totalQuota_ = (2LL * 1024 * 1024 * 1024);
    usedQuota_ = 0;
    _remotefileListJob = new OCC::DiscoveryFolderFileList(accountState_->account());
    _remotefileListJob->setParent(this);
    connect(this, &VfsMac::startRemoteFileListJob, _remotefileListJob, &OCC::DiscoveryFolderFileList::doGetFolderContent);
    connect(_remotefileListJob, &OCC::DiscoveryFolderFileList::gotDataSignal, this, &VfsMac::folderFileListFinish);

    // "talk" to the sync engine
    _syncWrapper = OCC::SyncWrapper::instance();
    connect(this, &VfsMac::addToFileTree, _syncWrapper, &OCC::SyncWrapper::updateFileTree, Qt::QueuedConnection);
    connect(_syncWrapper, &OCC::SyncWrapper::syncFinish, this, &VfsMac::slotSyncFinish, Qt::QueuedConnection);

    connect(this, &VfsMac::createItem, _syncWrapper, &OCC::SyncWrapper::createItemAtPath, Qt::QueuedConnection);
    connect(this, &VfsMac::openFile, _syncWrapper, &OCC::SyncWrapper::openFileAtPath, Qt::QueuedConnection);
    connect(this, &VfsMac::releaseFile, _syncWrapper, &OCC::SyncWrapper::releaseFileAtPath, Qt::QueuedConnection);
    connect(this, &VfsMac::deleteItem, _syncWrapper, &OCC::SyncWrapper::deleteItemAtPath, Qt::QueuedConnection);
    connect(this, &VfsMac::move, _syncWrapper, &OCC::SyncWrapper::moveItemAtPath, Qt::QueuedConnection);
}

bool VfsMac::enableAllocate() {
    return internal_->supportsAllocate();
}

bool VfsMac::enableCaseSensitiveNames() {
    return internal_->supportsCaseSensitiveNames();
}

bool VfsMac::enableExchangeData() {
    return internal_->supportsExchangeData();
}

bool VfsMac::enableExtendedTimes() {
    return internal_->supportsExtendedTimes();
}

bool VfsMac::enableSetVolumeName() {
    return internal_->supportsSetVolumeName();
}

QVariantMap VfsMac::currentContext()
{
    struct fuse_context* context = fuse_get_context();
    QVariantMap dict;
    if (!context) {
        return dict;
    }

    dict.insert(kGMUserFileSystemContextUserIDKey, QVariant((unsigned int) context->uid));
    dict.insert(kGMUserFileSystemContextGroupIDKey, QVariant((unsigned int) context->gid));
    dict.insert(kGMUserFileSystemContextProcessIDKey, QVariant((unsigned int) context->pid));

    return dict;
}

void VfsMac::mountAtPath(QString mountPath, QStringList options)
{
    this->mountAtPath(mountPath, options, true, true);
}

void VfsMac::mountAtPath(QString mountPath, QStringList options, bool shouldForeground, bool detachNewThread)
{
    internal_->setRootPath(rootPath_);
    internal_->setMountPath(mountPath);
    QStringList optionCopy;
    foreach(const QString option, options)
    {
        QString optionLower = option.toLower();
        if (optionLower == "rdonly" ||
            optionLower == "ro") {
            internal_->setIsReadOnly(true);
        }
        optionCopy.append(option);
    }
    QVariantMap args;
    args.insert("options", optionCopy);
    args.insert("shouldForeground", shouldForeground);
    if (detachNewThread) {
        std::thread t(&VfsMac::mount, this, args);
        t.detach();
    } else {
        this->mount(args);
    }
}

void VfsMac::unmount()
{
    if (internal_.data() != nullptr && internal_->status() == GMUserFileSystem_MOUNTED) {
        int ret = ::unmount(internal_->mountPath().toLatin1().data(), 0);
        if (ret != 0)
        {
            QVariantMap userData = errorWithCode(errno);
            QString description = userData.value("localizedDescription").toString() + " " + tr("Unable to unmount an existing 'dead' filesystem.");
            userData.insert("localizedDescription", description);
            emit FuseFileSystemMountFailed(userData);
            return;
        }
        //fuse_unmount(internal_->mountPath().toLatin1(), nullptr);
    }
}

bool VfsMac::invalidateItemAtPath(QString path, QVariantMap &error)
{
    int ret = -ENOTCONN;

    struct fuse* handle = internal_->handle();
    if (handle) {
        ret = fuse_invalidate(handle, path.toLatin1().data());

        // Note: fuse_invalidate_path() may return -ENOENT to indicate that there
        // was no entry to be invalidated, e.g., because the path has not been seen
        // before or has been forgotten. This should not be considered to be an
        // error.
        if (ret == -ENOENT) {
            ret = 0;
        }
    }

    if (ret != 0)
    {
        error = VfsMac::errorWithCode(ret);
        return false;
    }
    return true;
}


QVariantMap VfsMac::errorWithCode(int code)
{
    QVariantMap error;
    error.insert("code", code);
    error.insert("localizedDescription", strerror(code));
    return error;
}

QVariantMap VfsMac::errorWithPosixCode(int code)
{
    QVariantMap error;
    error.insert("code", code);
    error.insert("domain", FileManager::FMPOSIXErrorDomain);
    error.insert("localizedDescription", strerror(code));
    return error;
}

#define FUSEDEVIOCGETHANDSHAKECOMPLETE _IOR('F', 2, u_int32_t)
static const int kMaxWaitForMountTries = 50;
static const int kWaitForMountUSleepInterval = 100000;  // 100 ms

void VfsMac::waitUntilMounted (int fileDescriptor)
{
    for (int i = 0; i < kMaxWaitForMountTries; ++i) {
        u_int32_t handShakeComplete = 0;
        int ret = ioctl(fileDescriptor, FUSEDEVIOCGETHANDSHAKECOMPLETE,
                        &handShakeComplete);
        if (ret == 0 && handShakeComplete) {
            internal_->setStatus(GMUserFileSystem_MOUNTED);

            // Successfully mounted, so post notification.
            QVariantMap userInfo;
            userInfo.insert(kGMUserFileSystemMountPathKey, internal_->mountPath());

            emit FuseFileSystemDidMount(userInfo);
            return;
        }
        usleep(kWaitForMountUSleepInterval);
    }
}

void VfsMac::fuseInit()
{
    struct fuse_context* context = fuse_get_context();

    internal_->setHandle(context->fuse);
    internal_->setStatus(GMUserFileSystem_INITIALIZING);
    QVariantMap error;
    QVariantMap attribs = this->attributesOfFileSystemForPath("/", error);

    if (!attribs.isEmpty()) {
        int supports = 0;

        supports = attribs.value(kGMUserFileSystemVolumeSupportsAllocateKey).toInt();
        internal_->setSupportsAllocate(supports);

        supports = attribs.value(kGMUserFileSystemVolumeSupportsCaseSensitiveNamesKey).toInt();
        internal_->setSupportsCaseSensitiveNames(supports);

        supports = attribs.value(kGMUserFileSystemVolumeSupportsExchangeDataKey).toInt();
        internal_->setSupportsExchangeData(supports);

        supports = attribs.value(kGMUserFileSystemVolumeSupportsExtendedDatesKey).toInt();
        internal_->setSupportsExtendedTimes(supports);

        supports = attribs.value(kGMUserFileSystemVolumeSupportsSetVolumeNameKey).toInt();
        internal_->setSupportsSetVolumeName(supports);
    }

    // The mount point won't actually show up until this winds its way
    // back through the kernel after this routine returns. In order to post
    // the kGMUserFileSystemDidMount notification we start a new thread that will
    // poll until it is mounted.
    struct fuse_session* se = fuse_get_session(context->fuse);
    struct fuse_chan* chan = fuse_session_next_chan(se, NULL);
    int fd = fuse_chan_fd(chan);

    std::thread t(&VfsMac::waitUntilMounted, this, fd);
    t.detach();
}

void VfsMac::fuseDestroy()
{
    internal_->setStatus(GMUserFileSystem_UNMOUNTING);

    QVariantMap userInfo;
    userInfo.insert(kGMUserFileSystemMountPathKey, internal_->mountPath());

    emit FuseFileSystemDidUnmount(userInfo);
}

#pragma mark Internal Stat Operations

bool VfsMac::fillStatfsBuffer(struct statfs *stbuf, QString path, QVariantMap &error)
{
    QVariantMap attributes = this->attributesOfFileSystemForPath(path, error);
    if (attributes.isEmpty()) {
        return false;
    }

    // Block size
    Q_ASSERT(attributes.contains(kGMUserFileSystemVolumeFileSystemBlockSizeKey));
    stbuf->f_bsize = (uint32_t)attributes.value(kGMUserFileSystemVolumeFileSystemBlockSizeKey).toUInt();
    stbuf->f_iosize = (int32_t)attributes.value(kGMUserFileSystemVolumeFileSystemBlockSizeKey).toInt();

    // Size in blocks
    Q_ASSERT(attributes.contains(FileManager::FMFileSystemSize));
    unsigned long long size = attributes.value(FileManager::FMFileSystemSize).toULongLong();
    stbuf->f_blocks = (uint64_t)(size / stbuf->f_bsize);

    // Number of free / available blocks
    Q_ASSERT(attributes.contains(FileManager::FMFileSystemFreeSize));
    unsigned long long freeSize = attributes.value(FileManager::FMFileSystemFreeSize).toULongLong();
    stbuf->f_bavail = stbuf->f_bfree = (uint64_t)(freeSize / stbuf->f_bsize);

    // Number of nodes
    Q_ASSERT(attributes.contains(FileManager::FMFileSystemNodes));
    unsigned long long numNodes = attributes.value(FileManager::FMFileSystemNodes).toULongLong();
    stbuf->f_files = (uint64_t)numNodes;

    // Number of free / available nodes
    Q_ASSERT(attributes.contains(FileManager::FMFileSystemFreeNodes));
    unsigned long long freeNodes = attributes.value(FileManager::FMFileSystemFreeNodes).toULongLong();
    stbuf->f_ffree = (uint64_t)freeNodes;

    return true;
}

bool VfsMac::fillStatBuffer(struct stat *stbuf, QString path, QVariant userData, QVariantMap &error)
{
    QVariantMap attributes = this->defaultAttributesOfItemAtPath(path, userData, error);
    if (attributes.empty()) {
        return false;
    }

    // Inode
    if(attributes.contains(FileManager::FMFileSystemFileNumber))
    {
        long long inode = attributes.value(FileManager::FMFileSystemFileNumber).toLongLong();
        stbuf->st_ino = inode;
    }

    // Permissions (mode)
    long perm = (long)attributes.value(FileManager::FMFilePosixPermissions).toLongLong();
    stbuf->st_mode = perm;
    QString fileType = attributes.value(FileManager::FMFileType).toString();
    if (fileType == FileManager::FMFileTypeDirectory) {
        stbuf->st_mode |= S_IFDIR;
    } else if (fileType == FileManager::FMFileTypeRegular) {
        stbuf->st_mode |= S_IFREG;
    } else if (fileType == FileManager::FMFileTypeSymbolicLink) {
        stbuf->st_mode |= S_IFLNK;
    } else {
        error = errorWithCode(EFTYPE);
        return false;
    }

    // Owner and Group
    // Note that if the owner or group IDs are not specified, the effective
    // user and group IDs for the current process are used as defaults.
    stbuf->st_uid = attributes.contains(FileManager::FMFileOwnerAccountID) ? attributes.value(FileManager::FMFileOwnerAccountID).toLongLong() : geteuid();
    stbuf->st_gid = attributes.contains(FileManager::FMFileGroupOwnerAccountID) ? attributes.value(FileManager::FMFileGroupOwnerAccountID).toLongLong() : getegid();

    // nlink
    long nlink = attributes.value(FileManager::FMFileReferenceCount).toLongLong();
    stbuf->st_nlink = nlink;

    // flags
    if (attributes.contains(kGMUserFileSystemFileFlagsKey)) {
        long flags = attributes.value(kGMUserFileSystemFileFlagsKey).toLongLong();
        stbuf->st_flags = flags;
    } else {
        // Just in case they tried to use NSFileImmutable or NSFileAppendOnly
        if (attributes.contains(FileManager::FMFileImmutable))
        {
            bool immutableFlag = attributes.value(FileManager::FMFileImmutable).toBool();
            if (immutableFlag)
                stbuf->st_flags |= UF_IMMUTABLE;

        }
        if (attributes.contains(FileManager::FMFileAppendOnly))
        {
            bool appendFlag = attributes.value(FileManager::FMFileAppendOnly).toBool();
            if (appendFlag)
                stbuf->st_flags |= UF_APPEND;
        }
    }

    // Note: We default atime, ctime to mtime if it is provided.
    if(attributes.contains(FileManager::FMFileModificationDate))
    {
        QDateTime mdate = attributes.value(FileManager::FMFileModificationDate).toDateTime();
        if (mdate.isValid()) {
            const double seconds_dp = mdate.toMSecsSinceEpoch()/1000;
            const time_t t_sec = (time_t) seconds_dp;
            const double nanoseconds_dp = ((seconds_dp - t_sec) * kNanoSecondsPerSecond);
            const long t_nsec = (nanoseconds_dp > 0 ) ? nanoseconds_dp : 0;

            stbuf->st_mtimespec.tv_sec = t_sec;
            stbuf->st_mtimespec.tv_nsec = t_nsec;
            stbuf->st_atimespec = stbuf->st_mtimespec;  // Default to mtime
            stbuf->st_ctimespec = stbuf->st_mtimespec;  // Default to mtime
        }
    }
    if(attributes.contains(kGMUserFileSystemFileAccessDateKey))
    {
        QDateTime adate = attributes.value(kGMUserFileSystemFileAccessDateKey).toDateTime();
        if (adate.isValid()) {
            const double seconds_dp = adate.toMSecsSinceEpoch()/1000;
            const time_t t_sec = (time_t) seconds_dp;
            const double nanoseconds_dp = ((seconds_dp - t_sec) * kNanoSecondsPerSecond);
            const long t_nsec = (nanoseconds_dp > 0 ) ? nanoseconds_dp : 0;
            stbuf->st_atimespec.tv_sec = t_sec;
            stbuf->st_atimespec.tv_nsec = t_nsec;
        }
    }
    if(attributes.contains(kGMUserFileSystemFileChangeDateKey))
    {
        QDateTime cdate = attributes.value(kGMUserFileSystemFileChangeDateKey).toDateTime();
        if (cdate.isValid()) {
            const double seconds_dp = cdate.toMSecsSinceEpoch()/1000;
            const time_t t_sec = (time_t) seconds_dp;
            const double nanoseconds_dp = ((seconds_dp - t_sec) * kNanoSecondsPerSecond);
            const long t_nsec = (nanoseconds_dp > 0 ) ? nanoseconds_dp : 0;
            stbuf->st_ctimespec.tv_sec = t_sec;
            stbuf->st_ctimespec.tv_nsec = t_nsec;
        }
    }

#ifdef _DARWIN_USE_64_BIT_INODE
    if(attributes.contains(FileManager::FMFileCreationDate))
    {
        QDateTime bdate = attributes.value(FileManager::FMFileCreationDate).toDateTime();
        if (bdate.isValid()) {
            const double seconds_dp = bdate.toMSecsSinceEpoch()/1000;
            const time_t t_sec = (time_t) seconds_dp;
            const double nanoseconds_dp = ((seconds_dp - t_sec) * kNanoSecondsPerSecond);
            const long t_nsec = (nanoseconds_dp > 0 ) ? nanoseconds_dp : 0;
            stbuf->st_birthtimespec.tv_sec = t_sec;
            stbuf->st_birthtimespec.tv_nsec = t_nsec;
        }
    }
#endif

    // File size
    // Note that the actual file size of a directory depends on the internal
    // representation of directories in the particular file system. In general
    // this is not the combined size of the files in that directory.
    if(attributes.contains(FileManager::FMFileSize))
    {
        long long size = attributes.value(FileManager::FMFileSize).toLongLong();
        stbuf->st_size = size;
    }

    // Set the number of blocks used so that Finder will display size on disk
    // properly. The man page says that this is in terms of 512 byte blocks.
    if (attributes.contains(kGMUserFileSystemFileSizeInBlocksKey))
    {
        long long blocks = attributes.value(kGMUserFileSystemFileSizeInBlocksKey).toLongLong();
        stbuf->st_blocks = blocks;
    }
    else if (stbuf->st_size > 0)
    {
        stbuf->st_blocks = stbuf->st_size / 512;
        if (stbuf->st_size % 512)
            ++(stbuf->st_blocks);
    }

    // Optimal file I/O size
    if (attributes.contains(kGMUserFileSystemFileOptimalIOSizeKey))
    {
        int ioSize = attributes.value(kGMUserFileSystemFileOptimalIOSizeKey).toInt();
        stbuf->st_blksize = ioSize;
    }

    return true;
}

#pragma mark Creating an Item

void VfsMac::createDirectoryAtPath(QString absolutePath, QVariantMap &error)
{
    Q_UNUSED(error);
    emit createItem(absolutePath);
}

void VfsMac::createFileAtPath(QString absolutePath, QVariantMap &error)
{
    Q_UNUSED(error);
    emit createItem(absolutePath);
}

#pragma mark Removing an Item

void VfsMac::removeDirectoryAtPath(QString absolutePath, QVariantMap &error)
{
    Q_UNUSED(absolutePath);
    Q_UNUSED(error);
    //TODO emit removeDirectoryAtPath
}

void VfsMac::removeItemAtPath(QString absolutePath, QVariantMap &error)
{
    Q_UNUSED(error);
    emit deleteItem(absolutePath);
}

#pragma mark Moving an Item

void VfsMac::moveItemAtPath(QString absolutePath1, QString absolutePath2, QVariantMap &error)
{
    Q_UNUSED(error);
    emit move(absolutePath1, absolutePath2);
}

#pragma mark Linking an Item

void VfsMac::linkItemAtPath(QString absolutePath1, QString absolutePath2, QVariantMap &error)
{
    Q_UNUSED(absolutePath1);
    Q_UNUSED(absolutePath2);
    Q_UNUSED(error);
    //TODO emit linkItemAtPath
}

#pragma mark Symbolic Links

void VfsMac::createSymbolicLinkAtPath(QString absolutePath1, QString absolutePath2, QVariantMap &error)
{
    Q_UNUSED(absolutePath1);
    Q_UNUSED(absolutePath2);
    Q_UNUSED(error);
    //TODO emit createSymbolicLinkAtPath
}

void VfsMac::destinationOfSymbolicLinkAtPath(QString absolutePath, QVariantMap &error)
{
    Q_UNUSED(absolutePath);
    Q_UNUSED(error);
    //TODO emit destinationOfSymbolicLinkAtPath
}

#pragma mark Directory Contents

void VfsMac::folderFileListFinish(OCC::DiscoveryDirectoryResult *dr)
{
    if(dr)
    {
        QString ruta = dr->path;
        _fileListMap.insert(dr->path, dr);

        _mutex.lock();
        _dirCondition.wakeAll();
        _mutex.unlock();
    }
    else
        qDebug() << "Error al obtener los resultados, viene nulo";
}

void *VfsMac::contentsOfDirectoryAtPath(QString absolutePath, QVariantMap &error)
{
    QString relativePath = absolutePath;
    relativePath.replace(cfgFile.defaultFileStreamMirrorPath(), "");

    _mutex.lock();
    emit startRemoteFileListJob(relativePath);
    _dirCondition.wait(&_mutex);
    _mutex.unlock();

    qDebug() << Q_FUNC_INFO << "DONE looking for " << relativePath << "in: " << _fileListMap.keys();

    if(_fileListMap.value(relativePath)->code != 0) {
        errorWithPosixCode(_fileListMap.value(relativePath)->code);
        return nullptr;
    }

    FileManager fm;
    if(!_fileListMap.value(relativePath)->list.empty()) {
        for(unsigned long i=0; i <_fileListMap.value(relativePath)->list.size(); i++) {
            QString completePath = rootPath_ + (relativePath.endsWith("/") ? relativePath:(relativePath + "/")) + QString::fromLatin1(_fileListMap.value(relativePath)->list.at(i)->path);
            QFileInfo fi(completePath);
            if (!fi.exists()) {
                if(_fileListMap.value(relativePath)->list.at(i)->type == ItemTypeDirectory) {
                    unsigned long perm = 16877 & ALLPERMS;
                    QVariantMap attribs;
                    attribs.insert(FileManager::FMFilePosixPermissions, (long long)perm);
                    fm.createDirectory(completePath, attribs, error);
                } else if (_fileListMap.value(relativePath)->list.at(i)->type == ItemTypeFile) {
                    QVariant fd;
                    unsigned long perm = ALLPERMS;
                    QVariantMap attribs;
                    attribs.insert(FileManager::FMFilePosixPermissions, (long long)perm);
                    fm.createFileAtPath(completePath, attribs, fd, error);
                    close(fd.toInt());
                }
				emit addToFileTree(_fileListMap.value(relativePath)->list.at(i)->type, completePath);
				// update file tree here?
				OCC::SyncWrapper::instance()->setFileRecord(_fileListMap.value(relativePath)->list.at(i).get(), rootPath_ + (relativePath.endsWith("/") ? relativePath:(relativePath + "/")));
            }
        }
    }
    _fileListMap.remove(relativePath);
}

#pragma mark File Contents

void VfsMac::slotSyncFinish(){
    _mutex.lock();
    _syncCondition.wakeAll();
    _mutex.unlock();
}

char * VfsMac::getProcessName(pid_t pid)
{
    char pathBuffer [PROC_PIDPATHINFO_MAXSIZE];
    proc_pidpath(pid, pathBuffer, sizeof(pathBuffer));

    char nameBuffer[256];

    int position = strlen(pathBuffer);
    while(position >= 0 && pathBuffer[position] != '/')
    {
        position--;
    }

    strcpy(nameBuffer, pathBuffer + position + 1);

    return nameBuffer;
}

void VfsMac::openFileAtPath(QString absolutePath, QVariantMap &error)
{
    Q_UNUSED(error);
   struct fuse_context *context = fuse_get_context();
   QString nameBuffer = QString::fromLatin1(getProcessName(context->pid));
   qDebug() << "Process Name openFileAtPath: " << nameBuffer;

   if(nameBuffer != "Finder" && nameBuffer != "QuickLookSatellite" && nameBuffer != "mds") {
       _mutex.lock();
       emit openFile(absolutePath);
       _syncCondition.wait(&_mutex);
       _mutex.unlock();
   }
}

void VfsMac::releaseFileAtPath(QString absolutePath, QVariantMap &error)
{
    Q_UNUSED(error);
    struct fuse_context *context = fuse_get_context();
    QString nameBuffer = QString::fromLatin1(getProcessName(context->pid));
    qDebug() << "Process Name releaseFileAtPath: " << nameBuffer;

    if(nameBuffer == "Finder") {
        qDebug() << "FUSE releaseFileAtPath: " << absolutePath;
        emit releaseFile(absolutePath);
    }
}

void VfsMac::readFileAtPath(QString absolutePath, QVariantMap &error)
{
    Q_UNUSED(absolutePath);
    Q_UNUSED(error);
    //TODO emit ReadFile
}

void VfsMac::writeFileAtPath(QString absolutePath, QVariantMap &error)
{
    Q_UNUSED(absolutePath);
    Q_UNUSED(error);
    //TODO emit writeFileAtPath
}

void VfsMac::preallocateFileAtPath(QString absolutePath, QVariantMap &error)
{
    Q_UNUSED(absolutePath);
    Q_UNUSED(error);
    //TODO emit preallocateFileAtPath
}

void VfsMac::exchangeDataOfItemAtPath(QString absolutePath1, QString absolutePath2, QVariantMap &error)
{
    Q_UNUSED(absolutePath1);
    Q_UNUSED(absolutePath2);
    Q_UNUSED(error);
    //TODO emit exchangeDataOfItemAtPath
}

#pragma mark Getting and Setting Attributes

QVariantMap VfsMac::attributesOfFileSystemForPath(QString path, QVariantMap &error)
{
    QVariantMap attributes;

    unsigned long long defaultSize =(2LL * 1024 * 1024 * 1024);
    attributes.insert(FileManager::FMFileSystemSize, defaultSize);
    attributes.insert(FileManager::FMFileSystemFreeSize, defaultSize);
    attributes.insert(FileManager::FMFileSystemNodes, defaultSize);
    attributes.insert(FileManager::FMFileSystemFreeNodes, defaultSize);
    attributes.insert(kGMUserFileSystemVolumeMaxFilenameLengthKey, (int)255);
    attributes.insert(kGMUserFileSystemVolumeFileSystemBlockSizeKey, (int)4096);

    bool supports = true;

    attributes.insert(kGMUserFileSystemVolumeSupportsExchangeDataKey, supports);
    attributes.insert(kGMUserFileSystemVolumeSupportsAllocateKey, supports);

    FileManager fm;
    QVariantMap customAttribs = fm.attributesOfFileSystemForPath(rootPath_ + path, error);
    //qDebug() << "Path: " << rootPath_ + path;
    if(customAttribs.isEmpty())
    {
        if(error.empty())
            error = errorWithCode(ENODEV);
        attributes.clear();
        return attributes;
    }

    for(auto attrib : customAttribs.keys())
    {
        //qDebug() << "Key: " <<attrib << "Value: " << customAttribs.value(attrib) << "\n";
        attributes.insert(attrib, customAttribs.value(attrib));
    }

    attributes.insert(FileManager::FMFileSystemSize, totalQuota());
    attributes.insert(FileManager::FMFileSystemFreeSize, totalQuota() - usedQuota());

    return attributes;
}

bool VfsMac::setAttributes(QVariantMap attributes, QString path, QVariant userInfo, QVariantMap &error)
{
    Q_UNUSED(userInfo);
    QString p = rootPath_ + path;

    // TODO: Handle other keys not handled by NSFileManager setAttributes call.

    long long offset = attributes.value(FileManager::FMFileSize).toLongLong();
    if ( attributes.contains(FileManager::FMFileSize) )
    {
        int ret = truncate(p.toLatin1().data(), offset);
        if ( ret < 0 )
        {
            error = errorWithPosixCode(errno);
            return false;
        }
    }
    int flags = attributes.value(kGMUserFileSystemFileFlagsKey).toInt();
    if (attributes.contains(kGMUserFileSystemFileFlagsKey))
    {
        int rc = chflags(p.toLatin1().data(), flags);
        if (rc < 0) {
            error = errorWithPosixCode(errno);
            return false;
        }
    }
    FileManager fm;
    return fm.setAttributes(attributes, p, error);
}

QVariantMap VfsMac::defaultAttributesOfItemAtPath(QString path, QVariant userData, QVariantMap &error)
{
    Q_UNUSED(userData);
    // Set up default item attributes.
    QVariantMap attributes;
    bool isReadOnly = internal_->isReadOnly();
   // qDebug() << "Path: " << rootPath_ + path;
    attributes.insert(FileManager::FMFilePosixPermissions, (isReadOnly ? 0555 : 0775));
    attributes.insert(FileManager::FMFileReferenceCount, (long long)1L);
    if (path == "/")
        attributes.insert(FileManager::FMFileType, FileManager::FMFileTypeDirectory);
    else
        attributes.insert(FileManager::FMFileType, FileManager::FMFileTypeRegular);

    QString p = rootPath_ + path;
    FileManager fm;
    QVariantMap *customAttribs = fm.attributesOfItemAtPath(p, error);
//    [[NSFileManager defaultManager] attributesOfItemAtPath:p error:error];

    // Maybe this is the root directory?  If so, we'll claim it always exists.
    if ((!customAttribs || customAttribs->empty()) && path=="/") {
        return attributes;  // The root directory always exists.
    }

    if (customAttribs && !customAttribs->empty())
    {
        for(auto attrib : customAttribs->keys())
        {
  /*          if (path == "/") {
                qDebug() << "Key: " <<attrib << "Value: " << customAttribs->value(attrib) << "\n";
            }*/
            attributes.insert(attrib, customAttribs->value(attrib));// addEntriesFromDictionary:customAttribs];
        }

    }
    else
    {
        if(error.empty())
            error = errorWithCode(ENOENT);
        return QVariantMap();
    }

    // If they don't supply a size and it is a file then we try to compute it.
    // qDebug() << attributes << "Si llego al final\n";
    return attributes;
}

QVariantMap* VfsMac::extendedTimesOfItemAtPath(QString path, QVariant userData, QVariantMap &error)
{
    Q_UNUSED(userData);
    FileManager fm;
    return fm.attributesOfItemAtPath(path, error);
}

#pragma mark Extended Attributes

QStringList* VfsMac::extendedAttributesOfItemAtPath(QString path, QVariantMap &error)
{
    QString p = rootPath_ + path;
    QStringList *retval = nullptr;

    ssize_t size = listxattr(p.toLatin1().data(), nullptr, 0, XATTR_NOFOLLOW);
    if ( size < 0 ) {
        error = errorWithPosixCode(errno);
        return retval;
    }
    char *data = new char[size];
    size = listxattr(p.toLatin1().data(), data, size, XATTR_NOFOLLOW);
    if ( size < 0 ) {
        error = errorWithPosixCode(errno);
        return retval;
    }
    char* ptr = data;
    QString s;
    retval = new QStringList();
    while ( ptr < (data + size) ) {
        s = QString(ptr);
        retval->append(s);
        ptr += (s.length() + 1);
    }
    return retval;
}

QByteArray* VfsMac::valueOfExtendedAttribute(QString name, QString path, off_t position, QVariantMap &error)
{
    QByteArray *data=nullptr;
    QString p = rootPath_ + path;

    ssize_t size = getxattr(p.toLatin1().data(), name.toLatin1().data(), nullptr, 0,
                            position, XATTR_NOFOLLOW);
    if ( size < 0 ) {
        error = errorWithPosixCode(errno);
        return data;
    }
    char* cdata = new char[size];
    size = getxattr(p.toLatin1().data(), name.toLatin1().data(), cdata, size,
                    position, XATTR_NOFOLLOW);

    if ( size < 0 )
    {
        error = errorWithPosixCode(errno);
        return data;
    }
    data = new QByteArray(cdata, size);
//    data.setRawData(cdata, size);
    return data;
}
bool VfsMac::setExtendedAttribute(QString name, QString path, QByteArray value, off_t position, int options, QVariantMap &error)
{
    // Setting com.apple.FinderInfo happens in the kernel, so security related
    // bits are set in the options. We need to explicitly remove them or the call
    // to setxattr will fail.
    // TODO: Why is this necessary?
    options &= ~(XATTR_NOSECURITY | XATTR_NODEFAULT);
    QString p = rootPath_ + path;
    int ret = setxattr(p.toLatin1().data(), name.toLatin1().data(),
                       value.data(), value.length(),
                       position, options | XATTR_NOFOLLOW);
    if ( ret < 0 ) {
        error = errorWithPosixCode(errno);
        return false;
    }
    return true;
}

bool VfsMac::removeExtendedAttribute(QString name, QString path, QVariantMap &error)
{
    QString p = rootPath_ + path;
    int ret = removexattr(p.toLatin1().data(), name.toLatin1().data(), XATTR_NOFOLLOW);
    if ( ret < 0 ) {
        error = errorWithPosixCode(errno);
        return false;
    }
    return true;
}

#pragma mark FUSE Operations

#define SET_CAPABILITY(conn, flag, enable)
#define MAYBE_USE_ERROR(var, error)

static void* fusefm_init(struct fuse_conn_info* conn)
{
    try {
        VfsMac::instance()->fuseInit();
    }
    catch (QException exception) { }

    SET_CAPABILITY(conn, FUSE_CAP_ALLOCATE, VfsMac::instance()->enableAllocate());
    SET_CAPABILITY(conn, FUSE_CAP_XTIMES, VfsMac::instance()->enableExtendedTimes());
    SET_CAPABILITY(conn, FUSE_CAP_VOL_RENAME, VfsMac::instance()->enableSetVolumeName());
    SET_CAPABILITY(conn, FUSE_CAP_CASE_INSENSITIVE, !VfsMac::instance()->enableCaseSensitiveNames());
    SET_CAPABILITY(conn, FUSE_CAP_EXCHANGE_DATA, VfsMac::instance()->enableExchangeData());

    FUSE_ENABLE_SETVOLNAME(conn);
    FUSE_ENABLE_XTIMES(conn);

    return VfsMac::instance();
}

static void fusefm_destroy(void* private_data)
{
    VfsMac* fs = (VfsMac *)private_data;
    try {
        fs->fuseDestroy();
        //fs->deleteLater();
    }
    catch (QException exception) { }
}

static int fusefm_mkdir(const char* path, mode_t mode)
{
    QVariantMap error;
    VfsMac::instance()->createDirectoryAtPath(QString::fromLatin1(path), error);

    int res;

    res = mkdir(path, mode);
    if (res == -1) {
        return -errno;
    }

    return 0;
}

static int fusefm_create(const char* path, mode_t mode, struct fuse_file_info* fi)
{
    QVariantMap error;
    VfsMac::instance()->createFileAtPath(QString::fromLatin1(path), error);

    int fd;

    fd = open(path, fi->flags, mode);
    if (fd == -1) {
        return -errno;
    }
    fi->fh = fd;

    return 0;
}

static int fusefm_rmdir(const char* path)
{
    QVariantMap error;
    VfsMac::instance()->removeDirectoryAtPath(QString::fromLatin1(path), error);

    int res;

    res = rmdir(path);
    if (res == -1) {
        return -errno;
    }

    return 0;
}

static int fusefm_unlink(const char* path)
{
    QVariantMap error;
    VfsMac::instance()->removeItemAtPath(QString::fromLatin1(path), error);

    int res;

    res = unlink(path);
    if (res == -1) {
        return -errno;
    }

    return 0;
}

static int fusefm_rename(const char* from, const char* to)
{
    QVariantMap error;
    VfsMac::instance()->moveItemAtPath(QString::fromLatin1(from), QString::fromLatin1(to), error);

    int res;

    res = rename(from, to);
    if (res == -1) {
        return -errno;
    }

    return 0;
}

static int fusefm_link(const char* from, const char* to)
{
    QVariantMap error;
    VfsMac::instance()->linkItemAtPath(QString::fromLatin1(from), QString::fromLatin1(to), error);

    int res;

    res = link(from, to);
    if (res == -1) {
        return -errno;
    }

    return 0;
}

static int fusefm_symlink(const char* from, const char* to)
{
    QVariantMap error;
    VfsMac::instance()->createSymbolicLinkAtPath(QString::fromLatin1(from), QString::fromLatin1(to), error);

    int res;

    res = symlink(from, to);
    if (res == -1) {
        return -errno;
    }

    return 0;
}

static int fusefm_readlink(const char *path, char *buf, size_t size)
{
    QVariantMap error;
    VfsMac::instance()->destinationOfSymbolicLinkAtPath(QString::fromLatin1(path), error);

    int res;

    res = readlink(path, buf, size - 1);

    if (res == -1) {
        return -errno;
    }

    buf[res] = '\0';

    return 0;
}

static int fusefm_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
    DIR *dp;
    struct dirent *de;

    (void) offset;
    (void) fi;

    dp = opendir(path);
    if (dp == NULL)
        return -errno;

    while ((de = readdir(dp)) != NULL) {
        struct stat st;
        memset(&st, 0, sizeof(st));
        st.st_ino = de->d_ino;
        st.st_mode = de->d_type << 12;
        if (filler(buf, de->d_name, &st, 0))
            break;
    }

    closedir(dp);

    return 0;
}

static int fusefm_open(const char *path, struct fuse_file_info *fi)
{
    QVariantMap error;
    VfsMac::instance()->openFileAtPath(QString::fromLatin1(path), error);

    int ret = 0;  // TODO: Default to 0 (success) since a file-system does
    // not necessarily need to implement open?

    int fd = open(path, fi->flags);
    if (fd == -1) {
        return -errno;
    }

    fi->fh = fd;

    return ret;
}

static int fusefm_release(const char *path, struct fuse_file_info *fi)
{
    QVariantMap error;
    VfsMac::instance()->releaseFileAtPath(QString::fromLatin1(path), error);

    (void)path;

    close(fi->fh);

    return 0;
}

static int fusefm_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info* fi)
{
    QVariantMap error;
    VfsMac::instance()->readFileAtPath(QString::fromLatin1(path), error);

    int res;

    (void)path;
    res = pread(fi->fh, buf, size, offset);
    if (res == -1) {
        res = -errno;
    }

    return res;
}

static int fusefm_write(const char* path, const char* buf, size_t size, off_t offset, struct fuse_file_info* fi)
{
    QVariantMap error;
    VfsMac::instance()->writeFileAtPath(QString::fromLatin1(path), error);

    int res;

    (void)path;

    res = pwrite(fi->fh, buf, size, offset);
    if (res == -1) {
        res = -errno;
    }

    return res;
}

static int fusefm_fsync(const char* path, int isdatasync, struct fuse_file_info* fi)
{
    int res;

    (void)path;

    (void)isdatasync;

    res = fsync(fi->fh);
    if (res == -1) {
        return -errno;
    }

    return 0;
}

static int fusefm_fallocate(const char* path, int mode, off_t offset, off_t length, struct fuse_file_info* fi)
{
    QVariantMap error;
    VfsMac::instance()->preallocateFileAtPath(QString::fromLatin1(path), error);

    fstore_t fstore;

    if (!(mode & PREALLOCATE)) {
        return -ENOTSUP;
    }

    fstore.fst_flags = 0;
    if (mode & ALLOCATECONTIG) {
        fstore.fst_flags |= F_ALLOCATECONTIG;
    }
    if (mode & ALLOCATEALL) {
        fstore.fst_flags |= F_ALLOCATEALL;
    }

    if (mode & ALLOCATEFROMPEOF) {
        fstore.fst_posmode = F_PEOFPOSMODE;
    } else if (mode & ALLOCATEFROMVOL) {
        fstore.fst_posmode = F_VOLPOSMODE;
    }

    fstore.fst_offset = offset;
    fstore.fst_length = length;

    if (fcntl(fi->fh, F_PREALLOCATE, &fstore) == -1) {
        return -errno;
    } else {
        return 0;
    }
}

static int fusefm_exchange(const char* path1, const char* path2, unsigned long opts)
{
    QVariantMap error;
    VfsMac::instance()->exchangeDataOfItemAtPath(path1, path2, error);

    int res;

    res = exchangedata(path1, path2, opts);
    if (res == -1) {
        return -errno;
    }

    return 0;
}

static int fusefm_setvolname(const char* name)
{
    int ret = -ENOSYS;
    try
    {
        QVariantMap error;
        QVariantMap attribs;
        attribs.insert(kGMUserFileSystemVolumeNameKey, QString::fromLatin1(name));
        if (VfsMac::instance()->setAttributes(attribs, "/", QVariantMap(), error)) {
            ret = 0;
        } else {
            MAYBE_USE_ERROR(ret, error);
        }
    }
    catch (QException exception) { }
    return ret;
}

static QDateTime dateWithTimespec(const struct timespec* spec)
{
    unsigned long long time_ns = spec->tv_nsec;
    unsigned long long time_sec = spec->tv_sec + (time_ns / kNanoSecondsPerSecond);
    return QDateTime::fromMSecsSinceEpoch(time_sec*1000);
}

static int fusefm_fsetattr_x(const char *path, struct setattr_x *attr, struct fuse_file_info *fi)
{
    int res;
    uid_t uid = -1;
    gid_t gid = -1;

    if (SETATTR_WANTS_MODE(attr)) {
        res = fchmod(fi->fh, attr->mode);
        if (res == -1) {
            return -errno;
        }
    }

    if (SETATTR_WANTS_UID(attr)) {
        uid = attr->uid;
    }

    if (SETATTR_WANTS_GID(attr)) {
        gid = attr->gid;
    }

    if ((uid != -1) || (gid != -1)) {
        res = fchown(fi->fh, uid, gid);
        if (res == -1) {
            return -errno;
        }
    }

    if (SETATTR_WANTS_SIZE(attr)) {
        res = ftruncate(fi->fh, attr->size);
        if (res == -1) {
            return -errno;
        }
    }

    if (SETATTR_WANTS_MODTIME(attr)) {
        struct timeval tv[2];
        if (!SETATTR_WANTS_ACCTIME(attr)) {
            gettimeofday(&tv[0], NULL);
        } else {
            tv[0].tv_sec = attr->acctime.tv_sec;
            tv[0].tv_usec = attr->acctime.tv_nsec / 1000;
        }
        tv[1].tv_sec = attr->modtime.tv_sec;
        tv[1].tv_usec = attr->modtime.tv_nsec / 1000;
        res = futimes(fi->fh, tv);
        if (res == -1) {
            return -errno;
        }
    }

    if (SETATTR_WANTS_CRTIME(attr)) {
        struct attrlist attributes;

        attributes.bitmapcount = ATTR_BIT_MAP_COUNT;
        attributes.reserved = 0;
        attributes.commonattr = ATTR_CMN_CRTIME;
        attributes.dirattr = 0;
        attributes.fileattr = 0;
        attributes.forkattr = 0;
        attributes.volattr = 0;

        res = fsetattrlist(fi->fh, &attributes, &attr->crtime,
                           sizeof(struct timespec), FSOPT_NOFOLLOW);

        if (res == -1) {
            return -errno;
        }
    }

    if (SETATTR_WANTS_CHGTIME(attr)) {
        struct attrlist attributes;

        attributes.bitmapcount = ATTR_BIT_MAP_COUNT;
        attributes.reserved = 0;
        attributes.commonattr = ATTR_CMN_CHGTIME;
        attributes.dirattr = 0;
        attributes.fileattr = 0;
        attributes.forkattr = 0;
        attributes.volattr = 0;

        res = fsetattrlist(fi->fh, &attributes, &attr->chgtime, sizeof(struct timespec), FSOPT_NOFOLLOW);

        if (res == -1) {
            return -errno;
        }
    }

    if (SETATTR_WANTS_BKUPTIME(attr)) {
        struct attrlist attributes;

        attributes.bitmapcount = ATTR_BIT_MAP_COUNT;
        attributes.reserved = 0;
        attributes.commonattr = ATTR_CMN_BKUPTIME;
        attributes.dirattr = 0;
        attributes.fileattr = 0;
        attributes.forkattr = 0;
        attributes.volattr = 0;

        res = fsetattrlist(fi->fh, &attributes, &attr->bkuptime, sizeof(struct timespec), FSOPT_NOFOLLOW);

        if (res == -1) {
            return -errno;
        }
    }

    if (SETATTR_WANTS_FLAGS(attr)) {
        res = fchflags(fi->fh, attr->flags);
        if (res == -1) {
            return -errno;
        }
    }

    return 0;
}

static int fusefm_setattr_x(const char *path, struct setattr_x *attr)
{
    int res;
    uid_t uid = -1;
    gid_t gid = -1;

    if (SETATTR_WANTS_MODE(attr)) {
        res = lchmod(path, attr->mode);
        if (res == -1) {
            return -errno;
        }
    }

    if (SETATTR_WANTS_UID(attr)) {
        uid = attr->uid;
    }

    if (SETATTR_WANTS_GID(attr)) {
        gid = attr->gid;
    }

    if ((uid != -1) || (gid != -1)) {
        res = lchown(path, uid, gid);
        if (res == -1) {
            return -errno;
        }
    }

    if (SETATTR_WANTS_SIZE(attr)) {
        res = truncate(path, attr->size);
        if (res == -1) {
            return -errno;
        }
    }

    if (SETATTR_WANTS_MODTIME(attr)) {
        struct timeval tv[2];
        if (!SETATTR_WANTS_ACCTIME(attr)) {
            gettimeofday(&tv[0], NULL);
        } else {
            tv[0].tv_sec = attr->acctime.tv_sec;
            tv[0].tv_usec = attr->acctime.tv_nsec / 1000;
        }
        tv[1].tv_sec = attr->modtime.tv_sec;
        tv[1].tv_usec = attr->modtime.tv_nsec / 1000;
        res = lutimes(path, tv);
        if (res == -1) {
            return -errno;
        }
    }

    if (SETATTR_WANTS_CRTIME(attr)) {
        struct attrlist attributes;

        attributes.bitmapcount = ATTR_BIT_MAP_COUNT;
        attributes.reserved = 0;
        attributes.commonattr = ATTR_CMN_CRTIME;
        attributes.dirattr = 0;
        attributes.fileattr = 0;
        attributes.forkattr = 0;
        attributes.volattr = 0;

        res = setattrlist(path, &attributes, &attr->crtime,
                          sizeof(struct timespec), FSOPT_NOFOLLOW);

        if (res == -1) {
            return -errno;
        }
    }

    if (SETATTR_WANTS_CHGTIME(attr)) {
        struct attrlist attributes;

        attributes.bitmapcount = ATTR_BIT_MAP_COUNT;
        attributes.reserved = 0;
        attributes.commonattr = ATTR_CMN_CHGTIME;
        attributes.dirattr = 0;
        attributes.fileattr = 0;
        attributes.forkattr = 0;
        attributes.volattr = 0;

        res = setattrlist(path, &attributes, &attr->chgtime, sizeof(struct timespec), FSOPT_NOFOLLOW);

        if (res == -1) {
            return -errno;
        }
    }

    if (SETATTR_WANTS_BKUPTIME(attr)) {
        struct attrlist attributes;

        attributes.bitmapcount = ATTR_BIT_MAP_COUNT;
        attributes.reserved = 0;
        attributes.commonattr = ATTR_CMN_BKUPTIME;
        attributes.dirattr = 0;
        attributes.fileattr = 0;
        attributes.forkattr = 0;
        attributes.volattr = 0;

        res = setattrlist(path, &attributes, &attr->bkuptime,
                          sizeof(struct timespec), FSOPT_NOFOLLOW);

        if (res == -1) {
            return -errno;
        }
    }

    if (SETATTR_WANTS_FLAGS(attr)) {
        res = lchflags(path, attr->flags);
        if (res == -1) {
            return -errno;
        }
    }

    return 0;
}

static int fusefm_listxattr(const char *path, char *list, size_t size)
{
    ssize_t res = listxattr(path, list, size, XATTR_NOFOLLOW);
    if (res > 0) {
        if (list) {
            size_t len = 0;
            char *curr = list;
            do {
                size_t thislen = strlen(curr) + 1;
                if (strcmp(curr, G_KAUTH_FILESEC_XATTR) == 0) {
                    memmove(curr, curr + thislen, res - len - thislen);
                    res -= thislen;
                    break;
                }
                curr += thislen;
                len += thislen;
            } while (len < res);
        } else {
            /*
             ssize_t res2 = getxattr(path, G_KAUTH_FILESEC_XATTR, NULL, 0, 0,
             XATTR_NOFOLLOW);
             if (res2 >= 0) {
             res -= sizeof(G_KAUTH_FILESEC_XATTR);
             }
             */
        }
    }

    if (res == -1) {
        return -errno;
    }

    return res;
}

static int fusefm_getxattr(const char *path, const char *name, char *value, size_t size, uint32_t position)
{
    int res;

    if (strcmp(name, A_KAUTH_FILESEC_XATTR) == 0) {

        char new_name[MAXPATHLEN];

        memcpy(new_name, A_KAUTH_FILESEC_XATTR, sizeof(A_KAUTH_FILESEC_XATTR));
        memcpy(new_name, G_PREFIX, sizeof(G_PREFIX) - 1);

        res = getxattr(path, new_name, value, size, position, XATTR_NOFOLLOW);

    } else {
        res = getxattr(path, name, value, size, position, XATTR_NOFOLLOW);
    }

    if (res == -1) {
        return -errno;
    }

    return res;
}

static int fusefm_setxattr(const char *path, const char *name, const char *value, size_t size, int flags, uint32_t position)
{
    int res;

    if (!strncmp(name, XATTR_APPLE_PREFIX, sizeof(XATTR_APPLE_PREFIX) - 1)) {
        flags &= ~(XATTR_NOSECURITY);
    }

    if (!strcmp(name, A_KAUTH_FILESEC_XATTR)) {

        char new_name[MAXPATHLEN];

        memcpy(new_name, A_KAUTH_FILESEC_XATTR, sizeof(A_KAUTH_FILESEC_XATTR));
        memcpy(new_name, G_PREFIX, sizeof(G_PREFIX) - 1);

        res = setxattr(path, new_name, value, size, position, XATTR_NOFOLLOW);

    } else {
        res = setxattr(path, name, value, size, position, XATTR_NOFOLLOW);
    }

    if (res == -1) {
        return -errno;
    }

    return 0;
}

static int fusefm_removexattr(const char *path, const char *name)
{
    int res;

    if (strcmp(name, A_KAUTH_FILESEC_XATTR) == 0) {
        char new_name[MAXPATHLEN];

        memcpy(new_name, A_KAUTH_FILESEC_XATTR, sizeof(A_KAUTH_FILESEC_XATTR));
        memcpy(new_name, G_PREFIX, sizeof(G_PREFIX) - 1);

        res = removexattr(path, new_name, XATTR_NOFOLLOW);
    } else {
        res = removexattr(path, name, XATTR_NOFOLLOW);
    }

    if (res == -1) {
        return -errno;
    }

    return 0;
}

#undef MAYBE_USE_ERROR

static int fusefm_getattr(const char *path, struct stat *stbuf)
{
    int res;

    res = lstat(path, stbuf);

    /*
     * The optimal I/O size can be set on a per-file basis. Setting st_blksize
     * to zero will cause the kernel extension to fall back on the global I/O
     * size which can be specified at mount-time (option iosize).
     */
    stbuf->st_blksize = 0;

    if (res == -1) {
        return -errno;
    }

    return 0;
}

static int fusefm_fgetattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
{
    int res;

    (void)path;

    res = fstat(fi->fh, stbuf);

    // Fall back to global I/O size. See loopback_getattr().
    stbuf->st_blksize = 0;

    if (res == -1) {
        return -errno;
    }

    return 0;
}

struct loopback_dirp {
    DIR *dp;
    struct dirent *entry;
    off_t offset;
};

static int fusefm_opendir(const char *path, struct fuse_file_info *fi)
{
    printf("fusefm_opendir %s\n", path);

    QVariantMap error;
    VfsMac::instance()->contentsOfDirectoryAtPath(QString::fromLatin1(path), error);

    int ret = -ENOENT;

    struct loopback_dirp *d = (loopback_dirp*)malloc(sizeof(struct loopback_dirp));
    if (d == NULL) {
        return -ENOMEM;
    }

    d->dp = opendir(path);
    if (d->dp == NULL) {
        ret = -errno;
        free(d);
        return ret;
    }

    d->offset = 0;
    d->entry = NULL;

    fi->fh = (unsigned long)d;

    return 0;
}

static inline struct loopback_dirp *get_dirp(struct fuse_file_info *fi)
{
    return (struct loopback_dirp *)(uintptr_t)fi->fh;
}

static int fusefm_releasedir(const char *path, struct fuse_file_info *fi)
{
    struct loopback_dirp *d = get_dirp(fi);

    (void)path;

    closedir(d->dp);
    free(d);

    return 0;
}

static int fusefm_mknod(const char *path, mode_t mode, dev_t rdev)
{
    int res;

    if (S_ISFIFO(mode)) {
        res = mkfifo(path, mode);
    } else {
        res = mknod(path, mode, rdev);
    }

    if (res == -1) {
        return -errno;
    }

    return 0;
}

static int fusefm_getxtimes(const char *path, struct timespec *bkuptime, struct timespec *crtime)
{
    int res;

    struct attrlist attributes;
    attributes.bitmapcount = ATTR_BIT_MAP_COUNT;
    attributes.reserved    = 0;
    attributes.commonattr  = 0;
    attributes.dirattr     = 0;
    attributes.fileattr    = 0;
    attributes.forkattr    = 0;
    attributes.volattr     = 0;

    struct xtimeattrbuf {
        uint32_t size;
        struct timespec xtime;
    } __attribute__ ((packed));

    struct xtimeattrbuf buf;

    attributes.commonattr = ATTR_CMN_BKUPTIME;
    res = getattrlist(path, &attributes, &buf, sizeof(buf), FSOPT_NOFOLLOW);
    if (res == 0) {
        (void)memcpy(bkuptime, &(buf.xtime), sizeof(struct timespec));
    } else {
        (void)memset(bkuptime, 0, sizeof(struct timespec));
    }

    attributes.commonattr = ATTR_CMN_CRTIME;
    res = getattrlist(path, &attributes, &buf, sizeof(buf), FSOPT_NOFOLLOW);
    if (res == 0) {
        (void)memcpy(crtime, &(buf.xtime), sizeof(struct timespec));
    } else {
        (void)memset(crtime, 0, sizeof(struct timespec));
    }

    return 0;
}

static int fusefm_statfs(const char *path, struct statvfs *stbuf)
{
    int res;

    res = statvfs(path, stbuf);
    if (res == -1) {
        return -errno;
    }

    return 0;
}

static int fusefm_flush(const char *path, struct fuse_file_info *fi)
{
    int res;

    (void)path;

    res = close(dup(fi->fh));
    if (res == -1) {
        return -errno;
    }

    return 0;
}

static struct fuse_operations fusefm_oper = {
    .init        = fusefm_init,
    .destroy     = fusefm_destroy,

    // Creating an Item
    .mkdir       = fusefm_mkdir,
    .create      = fusefm_create,

    // Removing an Item
    .unlink      = fusefm_unlink,
    .rmdir       = fusefm_rmdir,

    // Moving an Item
    .rename      = fusefm_rename,

    // Linking an Item
    .link        = fusefm_link,

    // Symbolic Links
    .symlink     = fusefm_symlink,
    .readlink    = fusefm_readlink,

    // Directory Contents
    .opendir     = fusefm_opendir,
    .readdir     = fusefm_readdir,
    .releasedir  = fusefm_releasedir,

    // File Contents
    .open        = fusefm_open,
    .release     = fusefm_release,
    .read        = fusefm_read,
    .write       = fusefm_write,
    .fsync       = fusefm_fsync,
    .fallocate   = fusefm_fallocate,
    .exchange    = fusefm_exchange,

    // Getting and Setting Attributes
    .statfs      = fusefm_statfs,
    .setvolname  = fusefm_setvolname,
    .getattr     = fusefm_getattr,
    .fgetattr    = fusefm_fgetattr,
    .getxtimes   = fusefm_getxtimes,
    .setattr_x   = fusefm_setattr_x,
    .fsetattr_x  = fusefm_fsetattr_x,

    // Extended Attributes
    .listxattr   = fusefm_listxattr,
    .getxattr    = fusefm_getxattr,
    .setxattr    = fusefm_setxattr,
    .removexattr = fusefm_removexattr,

    //.access      = loopback_access,
    .mknod       = fusefm_mknod,
    .flush       = fusefm_flush,

    .flag_nullpath_ok = 1,
    .flag_nopath = 1,
};

/*VfsMac::~VfsMac()
{
    internal_->deleteLater();
}*/

#pragma mark Internal Mount

void VfsMac::mount(QVariantMap args)
{
    Q_ASSERT(internal_->status() == GMUserFileSystem_NOT_MOUNTED);
    internal_->setStatus(GMUserFileSystem_MOUNTING);

    QStringList options = args.value("options").toStringList();
    bool isThreadSafe = internal_->isThreadSafe();
    bool shouldForeground = args.value("shouldForeground").toBool();

    // Maybe there is a dead FUSE file system stuck on our mount point?
    struct statfs statfs_buf;
    memset(&statfs_buf, 0, sizeof(statfs_buf));
    int ret = statfs(internal_->mountPath().toLatin1().data(), &statfs_buf);
    if (ret == 0)
    {
        if (statfs_buf.f_fssubtype == (unsigned int)(-1))
        {
            // We use a special indicator value from FUSE in the f_fssubtype field to
            // indicate that the currently mounted filesystem is dead. It probably
            // crashed and was never unmounted.
            ret = ::unmount(internal_->mountPath().toLatin1().data(), 0);
            if (ret != 0)
            {
                QVariantMap userData = errorWithCode(errno);
                QString description = userData.value("localizedDescription").toString() + " " + tr("Unable to unmount an existing 'dead' filesystem.");
                userData.insert("localizedDescription", description);
                emit FuseFileSystemMountFailed(userData);
                return;
            }
            if (internal_->mountPath().startsWith("/Volumes/"))
            {
                // Directories for mounts in @"/Volumes/..." are removed automatically
                // when an unmount occurs. This is an asynchronous process, so we need
                // to wait until the directory is removed before proceeding. Otherwise,
                // it may be removed after we try to create the mount directory and the
                // mount attempt will fail.
                bool isDirectoryRemoved = false;
                static const int kWaitForDeadFSTimeoutSeconds = 5;
                struct stat stat_buf;
                for (int i = 0; i < 2 * kWaitForDeadFSTimeoutSeconds; ++i)
                {
                    usleep(500000);  // .5 seconds
                    ret = stat(internal_->mountPath().toLatin1().data(), &stat_buf);
                    if (ret != 0 && errno == ENOENT)
                    {
                        isDirectoryRemoved = true;
                        break;
                    }
                }
                if (!isDirectoryRemoved) {
                    QString description = tr("Gave up waiting for directory under /Volumes to be removed after "
                                             "cleaning up a dead file system mount.");
                    QVariantMap userData = errorWithCode(GMUserFileSystem_ERROR_UNMOUNT_DEADFS_RMDIR);
                    userData.insert("localizedDescription", description);
                    emit FuseFileSystemMountFailed(userData);
                    return;
                }
            }
        }
    }

    // Check mount path as necessary.
    struct stat stat_buf;
    memset(&stat_buf, 0, sizeof(stat_buf));
    ret = stat(internal_->mountPath().toLatin1().data(), &stat_buf);
    if ((ret == 0 && !S_ISDIR(stat_buf.st_mode)) ||
        (ret != 0 && errno == ENOTDIR))
    {
        emit FuseFileSystemMountFailed(errorWithCode(ENOTDIR));
        return;
    }

    // Trigger initialization of NSFileManager. This is rather lame, but if we
    // don't call directoryContents before we mount our FUSE filesystem and
    // the filesystem uses NSFileManager we may deadlock. It seems that the
    // NSFileManager class will do lazy init and will query all mounted
    // filesystems. This leads to deadlock when we re-enter our mounted FUSE file
    // system. Once initialized it seems to work fine.
    QDir dir("/Volumes");
    dir.entryList();

    QStringList arguments;
    arguments.append(QCoreApplication::applicationFilePath());

    if (!isThreadSafe)
        arguments.append("-s");
    if (shouldForeground)
        arguments.append("-f"); // Foreground rather than daemonize.
    for (int i = 0; i < options.length(); ++i)
    {
        QString option = options.at(i);
        if (!option.isEmpty())
            arguments.append(QString("-o") + option);
    }
    arguments.append(internal_->mountPath());
    arguments.append(internal_->rootPath());

    // Start Fuse Main
    int argc = arguments.length();
    char** argv = new char*[argc];
    for (int i = 0, count = argc; i < count; i++)
    {
        QString argument = arguments.at(i);
        argv[i] = strdup(argument.toLatin1().data());  // We'll just leak this for now.
    }
    ret = fuse_main(argc, (char **)argv, &fusefm_oper, this);

    if (internal_ && internal_->status() == GMUserFileSystem_MOUNTING) {
        // If we returned from fuse_main while we still think we are
        // mounting then an error must have occurred during mount.
        QString description = QString("Internal FUSE error (rc=%1) while attempting to mount the file system. "
                                 "For now, the best way to diagnose is to look for error messages using "
                                 "Console.").arg(errno);
        QVariantMap userData = errorWithCode(errno);
        userData.insert("localizedDescription", QVariant(userData.value("localizedDescription").toString() + description));
        emit FuseFileSystemMountFailed(userData);
    } else if (internal_)
        internal_->setStatus(GMUserFileSystem_NOT_MOUNTED);
}
