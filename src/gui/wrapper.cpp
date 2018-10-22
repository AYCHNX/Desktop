/*
 * Copyright (C) by Olivier Goffart <ogoffart@woboq.com>
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

#include "wrapper.h"
#include "configfile.h"
#include "folderman.h"
#include "accountmanager.h"
#include "socketapi.h"
#include "common/syncjournaldb.h"

#include <QtCore>

namespace OCC {

Q_LOGGING_CATEGORY(lcWrapper, "nextcloud.gui.wrapper", QtInfoMsg)

Wrapper *Wrapper::instance()
{
    static Wrapper instance;
    return &instance;
}

void Wrapper::addFolder(){

    //TODO it assumes only one account
    auto accountState = AccountManager::instance()->accounts().first();

    if (accountState == nullptr || accountState->isSignedOut()) {
        qCInfo(lcWrapper) << "Account didn't connect!";
        return;
    }

    FolderMan *folderMan = FolderMan::instance();
    ConfigFile cfgFile;
    QString localFolder = FolderDefinition::prepareLocalPath(cfgFile.defaultFileStreamMirrorPath());
    // Sync everything
    QString remoteFolder = QString("/");

    qCInfo(lcWrapper) << "Adding folder definition for" << localFolder << remoteFolder;
    FolderDefinition folderDefinition;
    folderDefinition.localPath = localFolder;
    folderDefinition.targetPath = FolderDefinition::prepareTargetPath(remoteFolder);
    folderDefinition.ignoreHiddenFiles = folderMan->ignoreHiddenFiles();

    if (folderMan->navigationPaneHelper().showInExplorerNavigationPane())
        folderDefinition.navigationPaneClsid = QUuid::createUuid();

    Folder *folder = folderMan->addFolder(accountState.data(), folderDefinition);
    folderMan->setCurrentSyncFolder(folder);
}

void Wrapper::download(QStringList paths){
    foreach(QString path, paths){
        OCC::SyncJournalDb::instance()->setSyncMode(path, OCC::SyncJournalDb::SyncMode::SYNCMODE_ONLINE);
        OCC::SyncJournalDb::instance()->setSyncModeDownload(path, OCC::SyncJournalDb::SyncModeDownload::SYNCMODE_DOWNLOADED_NO);
        qCInfo(lcWrapper) << "Path: " << path << "has SyncMode"<< OCC::SyncJournalDb::instance()->getSyncMode(path);
        qCInfo(lcWrapper) << "Path: " << path << "has SyncModeDownload"<< OCC::SyncJournalDb::instance()->getSyncModeDownload(path);
    }

    FolderMan *folderMan = FolderMan::instance();
    qCInfo(lcWrapper) << "Current sync folder: " << folderMan->currentSyncFolder()->path();
    folderMan->startSyncNow(paths);
}

void Wrapper::upload(QString localPath){

}




}
