#include "syncwrapper.h"
#include "socketapi.h"
#include "csync/vio/csync_vio_local.h"
#include <qdir.h>

namespace OCC {
	Q_LOGGING_CATEGORY(lcSyncWrapper, "nextcloud.gui.wrapper", QtInfoMsg);

	SyncWrapper *SyncWrapper::instance()
	{
		static SyncWrapper instance;
		qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &, const QString & msg) {
                            QFile outFile("C:/Nextcloud/tree.txt");
                            outFile.open(QIODevice::WriteOnly | QIODevice::Append);
                            QTextStream ts(&outFile);
                            ts << msg << endl;});

		return &instance;
	}

	QString SyncWrapper::getRelativePath(QString path)
	{
		QString localPath = QDir::cleanPath(path);
		//if (localPath.endsWith('/'))
		//	localPath.chop(1);

		//if (localPath.startsWith('/'))
		//	localPath.remove(0, 1);

		Folder *folderForPath = FolderMan::instance()->folderForPath(localPath);

		//QString folderRelativePath("");
		//if (folderForPath)
		//	folderRelativePath = localPath.mid(folderForPath->cleanPath().length() + 1);

		return folderForPath->cleanPath();
	}

	void SyncWrapper::updateFileTree(bool newFile, const QString rootPath, const QString path, csync_file_stat_t *remoteNode)
	{
		QString folderRelativePath = path;
		qDebug() << Q_FUNC_INFO << "rootPath: " << rootPath;
		qDebug() << Q_FUNC_INFO << "path: " << path;
		qDebug() << Q_FUNC_INFO << "getRelativePath(path): " << getRelativePath(path);
		if (SyncJournalDb::instance()->getSyncMode(remoteNode->path) != SyncJournalDb::SyncMode::SYNCMODE_OFFLINE) {
			FolderMan::instance()->currentSyncFolder()->updateLocalFileTree(remoteNode->path, CSYNC_INSTRUCTION_IGNORE);
		} else {
			FolderMan::instance()->currentSyncFolder()->updateLocalFileTree(remoteNode->path, CSYNC_INSTRUCTION_NEW);
		}

		if (newFile) {
			setFileRecord(remoteNode, rootPath);
		}
		
		FolderMan::instance()->currentSyncFolder()->updateFuseCreatedFile(remoteNode->path, newFile);
	}


	void SyncWrapper::createItemAtPath(const QString path)
	{
		FolderMan::instance()->currentSyncFolder()->updateFuseCreatedFile(getRelativePath(path), false);
		sync(path, false, CSYNC_INSTRUCTION_NEW);
	}

	void SyncWrapper::openFileAtPath(const QString path)
	{
		sync(path, true, CSYNC_INSTRUCTION_NEW);
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

	void SyncWrapper::moveItemAtPath(const QString oldPath, const QString newPath)
	{
		FolderMan::instance()->currentSyncFolder()->updateFuseCreatedFile(getRelativePath(oldPath), false);
		FolderMan::instance()->currentSyncFolder()->updateFusePath(getRelativePath(oldPath), getRelativePath(newPath));
		sync(newPath, false, CSYNC_INSTRUCTION_EVAL_RENAME);
	}

	
    void SyncWrapper::setFileRecord(csync_file_stat_t *remoteNode, const QString localPath) {
            //otherwise rename and move will never work
			qDebug() << Q_FUNC_INFO << "remoteNode->path: " << remoteNode->path;
            OCC::SyncJournalFileRecord rec;
            if (SyncJournalDb::instance()->getFileRecord(remoteNode->path, &rec)) {
                    QByteArray fullPath(localPath.toLatin1() + remoteNode->path);
					qDebug() << Q_FUNC_INFO << "fullPath: " << fullPath;
                    if (csync_vio_local_stat(fullPath.constData(), remoteNode) == 0) {
                            rec._inode = remoteNode->inode;
                            qCDebug(lcSyncWrapper) << remoteNode->path << "Retrieved inode " << remoteNode->inode;
                    }

                    rec._path = remoteNode->path;
                    rec._fileId = remoteNode->file_id;
                    rec._modtime = remoteNode->modtime;
                    rec._type = remoteNode->type;
                    rec._fileSize = remoteNode->size;
                    rec._remotePerm = remoteNode->remotePerm;
                    rec._checksumHeader = remoteNode->checksumHeader;
                    SyncJournalDb::instance()->setFileRecordMetadata(rec);
            }
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