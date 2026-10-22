#include "player.h"

#include <QByteArray>
#include <QMetaObject>
#include <QMetaType>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <clocale>
#include <limits>
#include <mutex>
#include <vector>

namespace mpvideo {

namespace {

QVariant nodeToVariant(const mpv_node &node, int depth = 0) {
    if (depth > 64)
        return {};

    switch (node.format) {
    case MPV_FORMAT_NONE:
        return {};
    case MPV_FORMAT_STRING:
        return node.u.string ? QVariant(QString::fromUtf8(node.u.string)) : QVariant(QString{});
    case MPV_FORMAT_FLAG:
        return node.u.flag != 0;
    case MPV_FORMAT_INT64:
        return QVariant::fromValue<qlonglong>(node.u.int64);
    case MPV_FORMAT_DOUBLE:
        return node.u.double_;
    case MPV_FORMAT_BYTE_ARRAY:
        if (!node.u.ba || !node.u.ba->data)
            return QByteArray{};
        return QByteArray(static_cast<const char *>(node.u.ba->data),
                          static_cast<qsizetype>(node.u.ba->size));
    case MPV_FORMAT_NODE_ARRAY: {
        const mpv_node_list *list = node.u.list;
        QVariantList values;
        if (!list || list->num <= 0 || !list->values)
            return values;

        values.reserve(list->num);
        for (int index = 0; index < list->num; ++index)
            values.push_back(nodeToVariant(list->values[index], depth + 1));
        return values;
    }
    case MPV_FORMAT_NODE_MAP: {
        const mpv_node_list *list = node.u.list;
        QVariantMap values;
        if (!list || list->num <= 0 || !list->values || !list->keys)
            return values;

        for (int index = 0; index < list->num; ++index) {
            const char *key = list->keys[index];
            if (key)
                values.insert(QString::fromUtf8(key), nodeToVariant(list->values[index], depth + 1));
        }
        return values;
    }
    default:
        return {};
    }
}

QString mpvErrorText(int errorCode) {
    const char *message = mpv_error_string(errorCode);
    return message ? QString::fromUtf8(message) : QStringLiteral("Unknown libmpv error");
}

}

struct Player::CallbackBridge {
    std::mutex mutex;
    Player *target = nullptr;
    std::atomic_bool eventQueued = false;
};

Player::Player(QObject *parent) : Player(QStringList{}, parent) {}

Player::Player(const QStringList &options, QObject *parent)
    : QObject(parent), callbackBridge_(std::make_unique<CallbackBridge>()) {
    callbackBridge_->target = this;
    // QApplication adopts the desktop locale; libmpv requires C numeric parsing.
    std::setlocale(LC_NUMERIC, "C");
    handle_ = mpv_create();
    if (!handle_) {
        const QString message = QStringLiteral("Could not create the libmpv player");
        QMetaObject::invokeMethod(this, [this, message] { emit error(message); }, Qt::QueuedConnection);
        return;
    }

    int optionResult = mpv_set_option_string(handle_, "vo", "libmpv");
    if (optionResult >= 0)
        optionResult = mpv_set_option_string(handle_, "terminal", "no");
    if (optionResult >= 0)
        optionResult = mpv_set_option_string(handle_, "input-default-bindings", "no");
    if (optionResult >= 0)
        optionResult = mpv_set_option_string(handle_, "input-vo-keyboard", "no");
    if (optionResult >= 0)
        optionResult = mpv_set_option_string(handle_, "hwdec", "auto-safe");

    for (const QString &option : options) {
        const qsizetype separator = option.indexOf('=');
        if (separator <= 0) {
            optionResult = MPV_ERROR_INVALID_PARAMETER;
            break;
        }
        const QByteArray name = option.first(separator).toUtf8();
        const QByteArray value = option.sliced(separator + 1).toUtf8();
        if (name.contains('\0') || value.contains('\0')) {
            optionResult = MPV_ERROR_INVALID_PARAMETER;
            break;
        }
        optionResult = mpv_set_option_string(handle_, name.constData(), value.constData());
        if (optionResult < 0)
            break;
    }
    if (optionResult < 0) {
        const QString message =
            QStringLiteral("Could not configure libmpv: %1").arg(mpvErrorText(optionResult));
        mpv_destroy(handle_);
        handle_ = nullptr;
        QMetaObject::invokeMethod(this, [this, message] { emit error(message); }, Qt::QueuedConnection);
        return;
    }

    const int initializeResult = mpv_initialize(handle_);
    if (initializeResult < 0) {
        const QString message =
            QStringLiteral("Could not initialize libmpv: %1").arg(mpvErrorText(initializeResult));
        mpv_destroy(handle_);
        handle_ = nullptr;
        QMetaObject::invokeMethod(this, [this, message] { emit error(message); }, Qt::QueuedConnection);
        return;
    }

    ready_ = true;
    mpv_set_wakeup_callback(handle_, &Player::wakeupCallback, callbackBridge_.get());
    observeProperties();
    mpv_request_event(handle_, MPV_EVENT_START_FILE, 1);
    mpv_request_event(handle_, MPV_EVENT_END_FILE, 1);
}

Player::~Player() {
    emit aboutToShutdown();

    if (callbackBridge_) {
        std::lock_guard lock(callbackBridge_->mutex);
        callbackBridge_->target = nullptr;
    }

    if (handle_) {
        mpv_set_wakeup_callback(handle_, nullptr, nullptr);
        mpv_terminate_destroy(handle_);
        handle_ = nullptr;
    }

    ready_ = false;
}

bool Player::isReady() const noexcept { return ready_ && handle_; }

mpv_handle *Player::handle() const noexcept { return handle_; }

QVariant Player::property(const QString &name) const { return properties_.value(name); }

QVariantList Player::playlist() const { return property(QStringLiteral("playlist")).toList(); }

QVariantList Player::tracks() const { return property(QStringLiteral("track-list")).toList(); }

bool Player::command(const QStringList &arguments) {
    if (!isReady() || arguments.isEmpty()) {
        reportError(QStringLiteral("Cannot run an empty command or use an unavailable player"));
        return false;
    }

    std::vector<QByteArray> utf8Arguments;
    std::vector<const char *> commandArguments;
    utf8Arguments.reserve(static_cast<size_t>(arguments.size()));
    commandArguments.reserve(static_cast<size_t>(arguments.size()) + 1);

    for (const QString &argument : arguments) {
        QByteArray encoded = argument.toUtf8();
        if (encoded.contains('\0')) {
            reportError(QStringLiteral("libmpv command arguments cannot contain a NUL byte"));
            return false;
        }
        utf8Arguments.push_back(std::move(encoded));
    }

    for (const QByteArray &argument : utf8Arguments)
        commandArguments.push_back(argument.constData());
    commandArguments.push_back(nullptr);

    uint64_t requestId = 0;
    const QString description = QStringLiteral("command '%1'").arg(arguments.constFirst());
    if (!enqueueRequest(description, requestId))
        return false;

    const int result = mpv_command_async(handle_, requestId, commandArguments.data());
    if (result < 0) {
        pendingRequests_.remove(static_cast<quint64>(requestId));
        reportError(QStringLiteral("Could not queue %1: %2").arg(description, mpvErrorText(result)));
        return false;
    }

    return true;
}

bool Player::setProperty(const QString &name, const QVariant &value) {
    if (!isReady() || name.isEmpty()) {
        reportError(QStringLiteral("Cannot set a property on an unavailable player"));
        return false;
    }

    const QByteArray encodedName = name.toUtf8();
    if (encodedName.contains('\0')) {
        reportError(QStringLiteral("libmpv property names cannot contain a NUL byte"));
        return false;
    }

    mpv_format format = MPV_FORMAT_NONE;
    int flagValue = 0;
    int64_t integerValue = 0;
    double doubleValue = 0.0;
    QByteArray stringValue;
    char *stringPointer = nullptr;
    void *data = nullptr;

    switch (value.metaType().id()) {
    case QMetaType::Bool:
        format = MPV_FORMAT_FLAG;
        flagValue = value.toBool() ? 1 : 0;
        data = &flagValue;
        break;
    case QMetaType::Int:
    case QMetaType::LongLong:
    case QMetaType::UInt:
    case QMetaType::ULongLong: {
        bool valid = false;
        const qlonglong converted = value.toLongLong(&valid);
        if (!valid || (value.metaType().id() == QMetaType::ULongLong &&
                       value.toULongLong() > static_cast<qulonglong>(std::numeric_limits<int64_t>::max()))) {
            reportError(
                QStringLiteral("Property '%1' has an integer value outside libmpv's range").arg(name));
            return false;
        }
        integerValue = static_cast<int64_t>(converted);
        format = MPV_FORMAT_INT64;
        data = &integerValue;
        break;
    }
    case QMetaType::Float:
    case QMetaType::Double:
        doubleValue = value.toDouble();
        if (!std::isfinite(doubleValue)) {
            reportError(QStringLiteral("Property '%1' must be a finite number").arg(name));
            return false;
        }
        format = MPV_FORMAT_DOUBLE;
        data = &doubleValue;
        break;
    case QMetaType::QString:
        stringValue = value.toString().toUtf8();
        if (stringValue.contains('\0')) {
            reportError(QStringLiteral("libmpv string values cannot contain a NUL byte"));
            return false;
        }
        format = MPV_FORMAT_STRING;
        stringPointer = stringValue.data();
        data = &stringPointer;
        break;
    default:
        reportError(QStringLiteral("Property '%1' only accepts scalar values").arg(name));
        return false;
    }

    uint64_t requestId = 0;
    const QString description = QStringLiteral("set property '%1'").arg(name);
    if (!enqueueRequest(description, requestId))
        return false;

    const int result = mpv_set_property_async(handle_, requestId, encodedName.constData(), format, data);
    if (result < 0) {
        pendingRequests_.remove(static_cast<quint64>(requestId));
        reportError(QStringLiteral("Could not queue %1: %2").arg(description, mpvErrorText(result)));
        return false;
    }

    return true;
}

void Player::wakeupCallback(void *context) noexcept {
    auto *bridge = static_cast<CallbackBridge *>(context);
    if (!bridge)
        return;

    std::lock_guard lock(bridge->mutex);
    Player *target = bridge->target;
    if (!target || bridge->eventQueued.exchange(true, std::memory_order_acq_rel))
        return;

    QMetaObject::invokeMethod(target, [target] { target->drainEvents(); }, Qt::QueuedConnection);
}

void Player::observeProperties() {
    registerObservation("media-title", MPV_FORMAT_STRING);
    registerObservation("pause", MPV_FORMAT_FLAG);
    registerObservation("time-pos", MPV_FORMAT_DOUBLE);
    registerObservation("duration", MPV_FORMAT_DOUBLE);
    registerObservation("volume", MPV_FORMAT_DOUBLE);
    registerObservation("mute", MPV_FORMAT_FLAG);
    registerObservation("speed", MPV_FORMAT_DOUBLE);
    registerObservation("loop-file", MPV_FORMAT_STRING);
    registerObservation("playlist", MPV_FORMAT_NODE);
    registerObservation("track-list", MPV_FORMAT_NODE);
    registerObservation("playlist-pos", MPV_FORMAT_INT64);
    registerObservation("eof-reached", MPV_FORMAT_FLAG);
    registerObservation("idle-active", MPV_FORMAT_FLAG);
}

void Player::drainEvents() {
    if (!handle_ || !callbackBridge_)
        return;

    bool stateChanged = false;
    for (;;) {
        mpv_event *event = nullptr;
        do {
            event = mpv_wait_event(handle_, 0.0);
            if (event && event->event_id != MPV_EVENT_NONE)
                stateChanged = processEvent(event) || stateChanged;
        } while (event && event->event_id != MPV_EVENT_NONE);

        callbackBridge_->eventQueued.store(false, std::memory_order_release);
        event = mpv_wait_event(handle_, 0.0);
        if (event && event->event_id != MPV_EVENT_NONE) {
            stateChanged = processEvent(event) || stateChanged;
            callbackBridge_->eventQueued.store(true, std::memory_order_release);
            continue;
        }

        if (callbackBridge_->eventQueued.exchange(false, std::memory_order_acq_rel))
            continue;
        break;
    }

    if (stateChanged)
        emit changed();
}

bool Player::processEvent(mpv_event *event) {
    if (!event)
        return false;

    switch (event->event_id) {
    case MPV_EVENT_PROPERTY_CHANGE: {
        auto *propertyEvent = static_cast<mpv_event_property *>(event->data);
        if (!propertyEvent || !propertyEvent->name)
            return false;

        const QString name = QString::fromUtf8(propertyEvent->name);
        if (propertyEvent->format == MPV_FORMAT_NONE || !propertyEvent->data) {
            const bool removed = properties_.remove(name) > 0;
            if (name == QStringLiteral("media-title"))
                properties_.remove(QStringLiteral("title"));
            return removed;
        }

        QVariant value;
        switch (propertyEvent->format) {
        case MPV_FORMAT_STRING: {
            const char *stringValue = *static_cast<char **>(propertyEvent->data);
            value = stringValue ? QVariant(QString::fromUtf8(stringValue)) : QVariant(QString{});
            break;
        }
        case MPV_FORMAT_FLAG:
            value = *static_cast<int *>(propertyEvent->data) != 0;
            break;
        case MPV_FORMAT_INT64:
            value = QVariant::fromValue<qlonglong>(*static_cast<int64_t *>(propertyEvent->data));
            break;
        case MPV_FORMAT_DOUBLE:
            value = *static_cast<double *>(propertyEvent->data);
            break;
        case MPV_FORMAT_NODE:
            value = nodeToVariant(*static_cast<mpv_node *>(propertyEvent->data));
            break;
        default:
            return false;
        }

        const bool changed = properties_.value(name) != value || !properties_.contains(name);
        properties_.insert(name, value);
        if (name == QStringLiteral("media-title")) {
            properties_.insert(QStringLiteral("title"), value);
        }
        return changed;
    }
    case MPV_EVENT_COMMAND_REPLY:
    case MPV_EVENT_SET_PROPERTY_REPLY: {
        const QString description = pendingRequests_.take(static_cast<quint64>(event->reply_userdata));
        if (!description.isEmpty() && event->error < 0)
            reportError(QStringLiteral("%1 failed: %2").arg(description, mpvErrorText(event->error)));
        return false;
    }
    case MPV_EVENT_END_FILE: {
        const auto *endFile = static_cast<const mpv_event_end_file *>(event->data);
        if (endFile && endFile->reason == MPV_END_FILE_REASON_ERROR)
            reportError(QStringLiteral("Playback failed: %1").arg(mpvErrorText(endFile->error)));
        return false;
    }
    case MPV_EVENT_SHUTDOWN:
        ready_ = false;
        return false;
    default:
        return false;
    }
}

bool Player::registerObservation(const char *name, mpv_format format) {
    const int result = mpv_observe_property(handle_, 0, name, format);
    if (result < 0) {
        reportError(
            QStringLiteral("Could not observe '%1': %2").arg(QString::fromUtf8(name), mpvErrorText(result)));
        return false;
    }
    return true;
}

bool Player::enqueueRequest(const QString &description, uint64_t &requestId) {
    if (nextRequestId_ == 0 || nextRequestId_ == std::numeric_limits<uint64_t>::max()) {
        reportError(QStringLiteral("libmpv request identifier space is exhausted"));
        return false;
    }

    requestId = nextRequestId_++;
    pendingRequests_.insert(static_cast<quint64>(requestId), description);
    return true;
}

void Player::reportError(const QString &message) { emit error(message); }

}
