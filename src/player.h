#pragma once

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVariant>

#include <mpv/client.h>

#include <cstdint>
#include <memory>

namespace mpvideo {

class Player final : public QObject {
    Q_OBJECT

  public:
    explicit Player(QObject *parent = nullptr);
    explicit Player(const QStringList &options, QObject *parent = nullptr);
    ~Player() override;

    Player(const Player &) = delete;
    Player &operator=(const Player &) = delete;

    bool isReady() const noexcept;
    mpv_handle *handle() const noexcept;

    QVariant property(const QString &name) const;
    QVariantList playlist() const;
    QVariantList tracks() const;

    bool command(const QStringList &arguments);
    bool setProperty(const QString &name, const QVariant &value);

  signals:
    void changed();
    void error(const QString &message);
    void aboutToShutdown();

  private:
    struct CallbackBridge;

    static void wakeupCallback(void *context) noexcept;

    void observeProperties();
    void drainEvents();
    bool processEvent(mpv_event *event);
    bool registerObservation(const char *name, mpv_format format);
    bool enqueueRequest(const QString &description, uint64_t &requestId);
    void reportError(const QString &message);

    mpv_handle *handle_ = nullptr;
    bool ready_ = false;
    uint64_t nextRequestId_ = 1;
    QHash<QString, QVariant> properties_;
    QHash<quint64, QString> pendingRequests_;
    std::unique_ptr<CallbackBridge> callbackBridge_;
};

}
