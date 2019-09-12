#ifndef SYNCWRAPPER_H
#define SYNCWRAPPER_H

#include <QObject>
#include <QMap>

#include "common/syncjournaldb.h"
#include "folderman.h"
#include "csync.h"

namespace OCC {

class SyncWrapper : public QObject
{
    Q_OBJECT
public:
    static SyncWrapper *instance();
    ~SyncWrapper() {
		qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &, const QString & msg) {});
	}

	void setFileRecord(csync_file_stat_t *remoteNode, const QString localPath);

public slots:
    //void updateSyncQueue();
    void updateFileTree(bool newFile, const QString rootPath, const QString path, csync_file_stat_t *remoteNode);

	void createItemAtPath(const QString path);
    void openFileAtPath(const QString path);
    void writeFileAtPath(const QString path);
	void releaseFileAtPath(const QString path);
    void deleteItemAtPath(const QString path);
    void moveItemAtPath(const QString oldPath, const QString newPath);

signals:
    void syncFinish();
	//void startSyncForFolder();

private:
    SyncWrapper() {
        connect(SyncJournalDb::instance(), &SyncJournalDb::syncStatusChanged, this, &SyncWrapper::syncFinish, Qt::DirectConnection);
        //connect(SyncJournalDb::instance(), &SyncJournalDb::syncStatusChanged, this, &SyncWrapper::updateSyncQueue, Qt::DirectConnection);
        //connect(this, &SyncWrapper::startSyncForFolder, FolderMan::instance()->currentSyncFolder(), &Folder::startSync, Qt::DirectConnection);
    }

    QString getRelativePath(QString path);
    bool shouldSync(const QString path);
    void sync(const QString path, bool is_fuse_created_file, csync_instructions_e instruction = CSYNC_INSTRUCTION_NEW);

    //QMap<QString, bool> _syncDone;
};
}

#endif // SYNCWRAPPER_H
