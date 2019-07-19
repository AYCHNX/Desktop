#include "syncwrapper.h"
#include "socketapi.h"
#include "csync/vio/csync_vio_local.h"
#include "csync/csync_util.h"
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
//		if (localPath.endsWith('/'))
//			localPath.chop(1);

//		if (localPath.startsWith('/'))
//			localPath.remove(0, 1);

		Folder *folderForPath = FolderMan::instance()->folderForPath(localPath);

		//QString folderRelativePath("");
		//if (folderForPath)
		//	folderRelativePath = localPath.mid(folderForPath->cleanPath().length() + 1);

		return folderForPath->cleanPath();
	}

	void SyncWrapper::updateFileTree(int type, const QString path)
	{
		//QString folderRelativePath = getRelativePath(path);
		QString folderRelativePath = path;
		qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &, const QString & msg) {
				QFile outFile("C:/Nextcloud/tree.txt");
				outFile.open(QIODevice::WriteOnly | QIODevice::Append);
				QTextStream ts(&outFile);
				ts << msg << endl;});
		
		qDebug() << ">> SyncWrapper::updateFileTree #######" << path;
		qDebug() << "path: " << path;
		qDebug() << "folderRelativePath: " << folderRelativePath;
		qDebug() << "sync mode: " << SyncJournalDb::instance()->getSyncMode(folderRelativePath);

		if (SyncJournalDb::instance()->getSyncMode(folderRelativePath) != SyncJournalDb::SyncMode::SYNCMODE_OFFLINE) {
			qDebug() << "instruction: " << csync_instruction_str(CSYNC_INSTRUCTION_IGNORE);
			FolderMan::instance()->currentSyncFolder()->updateLocalFileTree(folderRelativePath, CSYNC_INSTRUCTION_IGNORE);
		} else {
			qDebug() << "instruction: " << csync_instruction_str(CSYNC_INSTRUCTION_NEW);
			FolderMan::instance()->currentSyncFolder()->updateLocalFileTree(folderRelativePath, CSYNC_INSTRUCTION_NEW);
		}

		qDebug() << "######################################################";
		qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &, const QString & msg) {});
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

	void SyncWrapper::moveItemAtPath(const QString oldPath, const QString newPath)
	{
		FolderMan::instance()->currentSyncFolder()->updateFuseCreatedFile(getRelativePath(oldPath), false);
		FolderMan::instance()->currentSyncFolder()->updateFusePath(getRelativePath(oldPath), getRelativePath(newPath));
		sync(newPath, false, CSYNC_INSTRUCTION_EVAL_RENAME);
	}

	
    void SyncWrapper::setFileRecord(csync_file_stat_t *remoteNode, const QString localPath) {
            //otherwise rename and move will never work
			qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &, const QString & msg) {
					QFile outFile("C:/Nextcloud/tree.txt");
					outFile.open(QIODevice::WriteOnly | QIODevice::Append);
					QTextStream ts(&outFile);
					ts << msg << endl;});
			qDebug() << ">> SyncWrapper::setFileRecord #######" << localPath;
            qDebug() << Q_FUNC_INFO << "results: " << remoteNode->path << remoteNode->type;
            OCC::SyncJournalFileRecord rec;
            if (SyncJournalDb::instance()->getFileRecord(remoteNode->path, &rec)) {
                    QByteArray fullPath(localPath.toLatin1() + remoteNode->path);
                    if (csync_vio_local_stat(fullPath.constData(), remoteNode) == 0) {
                            rec._inode = remoteNode->inode;
                            qCDebug(lcSyncWrapper) << remoteNode->path << "Retrieved inode " << remoteNode->inode;
                    }

                    rec._path = remoteNode->path;
                    //rec._etag = remoteNode->etag; no etag on the first run...
                    rec._fileId = remoteNode->file_id;
                    rec._modtime = remoteNode->modtime;
                    rec._type = remoteNode->type;
                    rec._fileSize = remoteNode->size;
                    rec._remotePerm = remoteNode->remotePerm;
                    rec._checksumHeader = remoteNode->checksumHeader;
                    SyncJournalDb::instance()->setFileRecordMetadata(rec);\
					FolderMan::instance()->currentSyncFolder()->updateFuseCreatedFile(fullPath, true);
            }
			qDebug() << "######################################################";
			qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &, const QString & msg) {});
    }


	void SyncWrapper::sync(const QString path, bool is_fuse_created_file, csync_instructions_e instruction)
	{
		QString folderRelativePath = getRelativePath(path);
		qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &, const QString & msg) {
						QFile outFile("C:/Nextcloud/tree.txt");
						outFile.open(QIODevice::WriteOnly | QIODevice::Append);
						QTextStream ts(&outFile);
						ts << msg << endl;});

		if (!folderRelativePath.isEmpty()) {

			if (SyncJournalDb::instance()->updateLastAccess(folderRelativePath) == 0)
				qCWarning(lcSyncWrapper) << "Couldn't update " << folderRelativePath << " file to last access.";

			if (SyncJournalDb::instance()->setSyncModeDownload(folderRelativePath, SyncJournalDb::SyncModeDownload::SYNCMODE_DOWNLOADED_NO) == 0)
				qCWarning(lcSyncWrapper) << "Couldn't set file " << folderRelativePath << " to SYNCMODE_DOWNLOADED_NO.";

			FolderMan::instance()->currentSyncFolder()->updateLocalFileTree(folderRelativePath, instruction);

			qDebug() << ">> SyncWrapper::sync #######" << path;
			qDebug() << "path: " << path;
			qDebug() << "folderRelativePath: " << folderRelativePath;
			qDebug() << "is_fuse_created_file: " << is_fuse_created_file;
			qDebug() << "instruction: " << csync_instruction_str(instruction);

			//_syncDone.insert(folderRelativePath, false);
			FolderMan::instance()->scheduleFolder();

			qDebug() << "######################################################";
			qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &, const QString & msg) {});

		} else {
			emit syncFinish();
		}

		qDebug() << "######################################################";
		qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &, const QString & msg) {});
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
