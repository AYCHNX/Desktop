#include "syncwrapper.h"
#include "socketapi.h"

#include <qdir.h>

namespace OCC {
	Q_LOGGING_CATEGORY(lcSyncWrapper, "nextcloud.gui.wrapper", QtInfoMsg);

	SyncWrapper *SyncWrapper::instance()
	{
		static SyncWrapper instance;
		return &instance;
	}

	QString SyncWrapper::getRelativePath(QString path)
	{
		QString localPath = QDir::cleanPath(path);
		if (localPath.endsWith('/'))
			localPath.chop(1);

		if (localPath.startsWith('/'))
			localPath.remove(0, 1);

		Folder *folderForPath = FolderMan::instance()->folderForPath(localPath);

		QString folderRelativePath("");
		if (folderForPath)
			folderRelativePath = localPath.mid(folderForPath->cleanPath().length() + 1);

		return folderRelativePath;
	}

	void SyncWrapper::updateFileTree(int type, const QString path)
	{
		//csync_instructions_e instruction = (type == 2) ? CSYNC_INSTRUCTION_NEW : CSYNC_INSTRUCTION_IGNORE;

		if (SyncJournalDb::instance()->getSyncMode(getRelativePath(path)) != SyncJournalDb::SyncMode::SYNCMODE_OFFLINE) {
			FolderMan::instance()->currentSyncFolder()->updateLocalFileTree(getRelativePath(path), CSYNC_INSTRUCTION_IGNORE);
		} else {
			FolderMan::instance()->currentSyncFolder()->updateLocalFileTree(getRelativePath(path), CSYNC_INSTRUCTION_NEW);
		}

		FolderMan::instance()->currentSyncFolder()->updateFuseCreatedFile(getRelativePath(path), true);
	}

	void SyncWrapper::createItemAtPath(const QString path)
	{
		FolderMan::instance()->currentSyncFolder()->updateFuseCreatedFile(getRelativePath(path), false);
		sync(path, false, CSYNC_INSTRUCTION_NEW);
	}

	void SyncWrapper::openFileAtPath(const QString path)
	{
		sync(path, true, CSYNC_INSTRUCTION_EVAL);
	}

	void SyncWrapper::writeFileAtPath(const QString path)
	{
		FolderMan::instance()->currentSyncFolder()->updateFuseCreatedFile(getRelativePath(path), false);
		sync(path, false, CSYNC_INSTRUCTION_EVAL);
	}

	void SyncWrapper::releaseFileAtPath(const QString path)
	{
		FolderMan::instance()->currentSyncFolder()->updateFuseCreatedFile(getRelativePath(path), false);
		sync(path, false, CSYNC_INSTRUCTION_EVAL);
	}

	void SyncWrapper::deleteItemAtPath(const QString path)
	{
		FolderMan::instance()->currentSyncFolder()->updateFuseCreatedFile(getRelativePath(path), false);
		sync(path, false, CSYNC_INSTRUCTION_NONE);
	}

	void SyncWrapper::moveItemAtPath(const QString path)
	{
		FolderMan::instance()->currentSyncFolder()->updateFuseCreatedFile(getRelativePath(path), false);
		sync(path, false, CSYNC_INSTRUCTION_EVAL_RENAME);
	}

	void SyncWrapper::sync(const QString path, bool is_fuse_created_file, csync_instructions_e instruction)
	{
		QString folderRelativePath = getRelativePath(path);
		if (!folderRelativePath.isEmpty()) {

			if (SyncJournalDb::instance()->updateLastAccess(folderRelativePath) == 0)
				qCWarning(lcSyncWrapper) << "Couldn't update file to last access.";

			if (SyncJournalDb::instance()->setSyncModeDownload(folderRelativePath, SyncJournalDb::SyncModeDownload::SYNCMODE_DOWNLOADED_NO) == 0)
				qCWarning(lcSyncWrapper) << "Couldn't set file to SYNCMODE_DOWNLOADED_NO.";

			FolderMan::instance()->currentSyncFolder()->updateLocalFileTree(folderRelativePath, instruction);
			//_syncDone.insert(folderRelativePath, false);
			FolderMan::instance()->scheduleFolder();

		} else {
			emit syncFinish();
		}
	}

	bool SyncWrapper::shouldSync(const QString path)
	{
		//FolderMan::instance()->currentSyncFolder()->updateLocalFileTree(path, CSYNC_INSTRUCTION_NEW);
		// Checks sync mode
		//if (SyncJournalDb::instance()->getSyncMode(path) != SyncJournalDb::SyncMode::SYNCMODE_OFFLINE)
		//    return false;

		// checks if file is cached
		// checks last access

		return true;
	}
}