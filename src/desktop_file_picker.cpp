#include "desktop_file_picker.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QFile>
#include <QPointer>
#include <QUrl>
#include <QUuid>

namespace mpvideo {
namespace {
const QString service = "org.freedesktop.portal.Desktop";
const QString requestInterface = "org.freedesktop.portal.Request";
void closePortalRequest(const QString &handle) {
    QDBusConnection::sessionBus().asyncCall(
        QDBusMessage::createMethodCall(service, handle, requestInterface, "Close"));
}
}
DesktopFilePicker::DesktopFilePicker(QObject *parent) : QObject(parent) {}
DesktopFilePicker::~DesktopFilePicker() {
    closeRequest();
    unsubscribe();
}
bool DesktopFilePicker::subscribe(const QString &handle) {
    handle_ = handle;
    return QDBusConnection::sessionBus().connect(service, handle_, requestInterface, "Response", this,
                                                 SLOT(response(uint, QVariantMap)));
}
void DesktopFilePicker::unsubscribe() {
    if (!handle_.isEmpty())
        QDBusConnection::sessionBus().disconnect(service, handle_, requestInterface, "Response", this,
                                                 SLOT(response(uint, QVariantMap)));
    handle_.clear();
}
void DesktopFilePicker::closeRequest() {
    if (active_ && !handle_.isEmpty())
        closePortalRequest(handle_);
}
void DesktopFilePicker::open(const QString &title, bool multiple, const QString &parentWindow) {
    if (active_)
        return;
    active_ = true;
    const auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        finish("Cannot connect to the desktop file chooser.");
        return;
    }
    const auto token = "mpvideo_" + QUuid::createUuid().toString(QUuid::Id128);
    auto sender = bus.baseService().mid(1);
    sender.replace('.', '_');
    // Subscribe before OpenFile so a fast response cannot be lost.
    if (!subscribe("/org/freedesktop/portal/desktop/request/" + sender + "/" + token)) {
        finish("Cannot listen for the desktop file chooser response.");
        return;
    }
    const QVariantMap options{{"handle_token", token},
                              {"multiple", multiple},
                              {"modal", true},
                              {"current_folder", QFile::encodeName(QDir::homePath()) + '\0'}};
    auto message = QDBusMessage::createMethodCall(service, "/org/freedesktop/portal/desktop",
                                                  "org.freedesktop.portal.FileChooser", "OpenFile");
    message.setArguments({parentWindow, title, options});
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(message));
    const QPointer<DesktopFilePicker> picker(this);
    const auto requestId = ++requestId_;
    connect(watcher, &QDBusPendingCallWatcher::finished, watcher, [picker, watcher, requestId] {
        const QDBusPendingReply<QDBusObjectPath> reply = *watcher;
        watcher->deleteLater();
        if (!picker || !picker->active_ || picker->requestId_ != requestId) {
            if (!reply.isError())
                closePortalRequest(reply.value().path());
            return;
        }
        if (reply.isError()) {
            picker->closeRequest();
            picker->finish("Could not open the desktop file chooser: " + reply.error().message());
            return;
        }
        const auto handle = reply.value().path();
        if (handle != picker->handle_) {
            picker->unsubscribe();
            if (!picker->subscribe(handle)) {
                picker->closeRequest();
                picker->finish("Cannot listen for the desktop file chooser response.");
            }
        }
    });
}
void DesktopFilePicker::response(uint code, const QVariantMap &results) {
    if (!active_)
        return;
    if (code == 1) {
        finish();
        return;
    }
    if (code != 0) {
        finish("The desktop file chooser could not select a file.");
        return;
    }
    QStringList paths;
    for (const auto &uri : qdbus_cast<QStringList>(results.value("uris"))) {
        const QUrl url(uri);
        if (!url.isValid() || !url.isLocalFile() || !QDir::isAbsolutePath(url.toLocalFile())) {
            finish("The desktop file chooser returned an invalid file location.");
            return;
        }
        paths << url.toLocalFile();
    }
    if (paths.isEmpty()) {
        finish("The desktop file chooser returned no files.");
        return;
    }
    emit selected(paths);
    finish();
}
void DesktopFilePicker::finish(const QString &error) {
    active_ = false;
    unsubscribe();
    if (!error.isEmpty())
        emit failed(error);
    emit finished();
}
void DesktopFilePicker::cancel() {
    if (!active_)
        return;
    closeRequest();
    finish();
}
}
