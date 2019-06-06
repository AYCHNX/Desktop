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

#include <QMessageBox>
#include <QApplication>

#include "vfs_maccontroller.h"
#include "vfs_mac.h"

#include <AvailabilityMacros.h>

VfsMacController* VfsMacController::_instance = 0;

VfsMacController *VfsMacController::instance()
{
    if (_instance == 0)
    {
        _instance = new VfsMacController();
    }
    
    return _instance;
}

void VfsMacController::mountFailed(QVariantMap userInfo)
{
    qDebug() << "Got mountFailed notification.";
    
    qDebug() << "kGMUserFileSystem Error code: " << userInfo.value("code") << ", userInfo=" << userInfo.value("localizedDescription");
    
    QMessageBox alert;
    alert.setText(userInfo.contains("localizedDescription")?userInfo.value("localizedDescription").toString() : "Unknown error");
    alert.exec();
    
    QApplication::quit();
}

void VfsMacController::didMount(QVariantMap userInfo)
{
    qDebug() << "Got didMount notification.";
    QString mountPath = userInfo.value(kGMUserFileSystemMountPathKey).toString();
}

void VfsMacController::didUnmount(QVariantMap userInfo)
{
    Q_UNUSED(userInfo);
    qDebug() << "Got didUnmount notification.";
    if(fs_->closeExternally) {
        QApplication::quit();
    }
    fs_->closeExternally = true;
}

void VfsMacController::mount()
{
    if(fs_)
        fs_->mountAtPath(mountPath, options);
}

void VfsMacController::unmount()
{
    if(fs_) {
        fs_->closeExternally = false;
        fs_->unmount();
    }
}

void VfsMacController::cleanCacheFolder()
{
    QDir mirror_path(rootPath);
    
    sleep(1000);
    mirror_path.removeRecursively();
}

void VfsMacController::slotquotaUpdated(qint64 total, qint64 used)
{
    fs_->setTotalQuota(total);
    fs_->setUsedQuota(used);
}

void VfsMacController::initialize(QString rootPath, QString mountPath, OCC::AccountState *accountState)
{
    this->rootPath = rootPath;
    this->mountPath = mountPath;
    
    fs_ = new VfsMac(rootPath, false, accountState, this);
    
    cfgFile.setDefaultFileStreamMirrorPath(rootPath);
    cfgFile.setDefaultFileStreamSyncPath(mountPath);
    
    QFileInfo root(rootPath);
    if(root.exists() && !root.isDir()) {
        QFile().remove(rootPath);
    }
    
    if(!root.exists()) {
        QDir().mkdir(rootPath);
    }
    
    qi_ = new OCC::QuotaInfo(accountState, this);
    
    connect(qi_, &OCC::QuotaInfo::quotaUpdated, this, &VfsMacController::slotquotaUpdated);
    connect(fs_, &VfsMac::FuseFileSystemDidMount, this, &VfsMacController::didMount);
    connect(fs_, &VfsMac::FuseFileSystemMountFailed, this, &VfsMacController::mountFailed);
    connect(fs_, &VfsMac::FuseFileSystemDidUnmount, this, &VfsMacController::didUnmount);
    
    qi_->setActive(true);
    
    QFileInfo icons(QCoreApplication::applicationDirPath() + "/../Resources/Nextcloud.icns");
    QString volArg = QString("volicon=%1").arg(icons.canonicalFilePath());
    
    options.append(volArg);
    
    // Do not use the 'native_xattr' mount-time option unless the underlying
    // file system supports native extended attributes. Typically, the user
    // would be mounting an HFS+ directory through VfsMac, so we do want
    // this option in that case.
    options.append("native_xattr");
    options.append("kill_on_unmount");
    options.append("local");
    options.append("volname=" + QApplication::applicationName() + " FS");
}

VfsMacController::VfsMacController()
{
    
}

VfsMacController::~VfsMacController()
{
    if(qi_) {
        disconnect(qi_, &OCC::QuotaInfo::quotaUpdated, this, &VfsMacController::slotquotaUpdated);
        delete(qi_);
    }
    
    if(fs_) {
        disconnect(fs_, &VfsMac::FuseFileSystemDidMount, this, &VfsMacController::didMount);
        disconnect(fs_, &VfsMac::FuseFileSystemMountFailed, this, &VfsMacController::mountFailed);
        disconnect(fs_, &VfsMac::FuseFileSystemDidUnmount, this, &VfsMacController::didUnmount);
    }
    
    _instance = 0;
}
