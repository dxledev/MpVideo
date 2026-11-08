#pragma once
#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPair>
#include <QVector>
#include <QWidget>
#include <algorithm>
#include <cmath>
#include <functional>
namespace mpvideo {
// Editing timeline shown in place of the seek bar. The clip is a list of kept segments; whatever
// lies between them is removed. Window turns the removed spans into the cut list the exporter
// already understands, so nothing downstream changes.
class TrimTimeline final : public QWidget {
  public:
    struct Segment {
        double start, end;
    };
    static constexpr double kMinSeconds = 0.1;
    static constexpr double kGap = 1e-3; // spans narrower than this count as touching, not removed

    explicit TrimTimeline(QWidget *parent = nullptr) : QWidget(parent) {
        setFixedHeight(kHeight);
        setMouseTracking(true);
        setAccessibleName("Trim timeline");
        setAccessibleDescription("Drag the end handles to trim. Right-click to split, delete or restore a segment.");
    }

    // Callbacks instead of signals keep this header-only.
    std::function<void(double seconds, bool final)> onSeek;
    std::function<void()> onChanged;

    void reset(double duration) {
        duration_ = qMax(0.0, duration);
        segments_.clear();
        if (duration_ > 0)
            segments_.append({0, duration_});
        undo_.clear();
        drag_ = None;
        update();
    }
    double duration() const { return duration_; }
    void setPlayhead(double seconds) {
        if (drag_ != None || seconds == playhead_)
            return;
        playhead_ = seconds;
        update();
    }
    const QVector<Segment> &segments() const { return segments_; }
    // Removed spans, in seconds, in order.
    QVector<QPair<double, double>> cuts() const {
        QVector<QPair<double, double>> out;
        double at = 0;
        for (const auto &s : segments_) {
            if (s.start - at > kGap)
                out.append({at, s.start});
            at = s.end;
        }
        if (duration_ - at > kGap && !segments_.isEmpty())
            out.append({at, duration_});
        return out;
    }
    double keptSeconds() const {
        double total = 0;
        for (const auto &s : segments_)
            total += s.end - s.start;
        return total;
    }
    bool isModified() const { return !cuts().isEmpty(); }
    bool canUndo() const { return !undo_.isEmpty(); }
    void undo() {
        if (undo_.isEmpty())
            return;
        segments_ = undo_.takeLast();
        changed();
    }
    int indexAt(double t) const {
        for (int i = 0; i < segments_.size(); ++i)
            if (t >= segments_[i].start && t <= segments_[i].end)
                return i;
        return -1;
    }
    bool splitAt(double t) {
        const int i = indexAt(t);
        if (i < 0 || t - segments_[i].start < minLen() || segments_[i].end - t < minLen())
            return false;
        remember();
        const double end = segments_[i].end;
        segments_[i].end = t;
        segments_.insert(i + 1, {t, end});
        changed();
        return true;
    }
    bool removeAt(double t) {
        const int i = indexAt(t);
        if (i < 0 || segments_.size() < 2)
            return false;
        remember();
        segments_.removeAt(i);
        changed();
        return true;
    }
    bool restoreAt(double t) {
        if (indexAt(t) >= 0)
            return false;
        int next = 0;
        while (next < segments_.size() && segments_[next].start < t)
            ++next;
        const double from = next > 0 ? segments_[next - 1].end : 0;
        const double to = next < segments_.size() ? segments_[next].start : duration_;
        if (to - from < kGap)
            return false;
        remember();
        segments_.insert(next, {from, to});
        changed();
        return true;
    }

  protected:
    void paintEvent(QPaintEvent *) override {
        if (duration_ <= 0)
            return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QPalette &pal = palette();
        const QColor accent = pal.color(QPalette::Highlight), onAccent = pal.color(QPalette::HighlightedText),
                     base = pal.color(QPalette::Base), text = pal.color(QPalette::WindowText),
                     mid = pal.color(QPalette::Mid);
        const QRectF lane(0, kPad, width(), height() - 2 * kPad);
        // Removed spans read as an empty trough with a dashed centre line.
        p.setPen(Qt::NoPen);
        p.setBrush(base);
        p.drawRoundedRect(lane, kRadius, kRadius);
        QColor dash = mid;
        dash.setAlpha(200);
        p.setPen(QPen(dash, 1.5, Qt::DashLine, Qt::RoundCap));
        p.drawLine(QPointF(kRadius, lane.center().y()), QPointF(width() - kRadius, lane.center().y()));
        QColor body = base;
        body.setRedF(base.redF() * 0.72 + accent.redF() * 0.28);
        body.setGreenF(base.greenF() * 0.72 + accent.greenF() * 0.28);
        body.setBlueF(base.blueF() * 0.72 + accent.blueF() * 0.28);
        for (int i = 0; i < segments_.size(); ++i) {
            const bool touchL = i > 0 && segments_[i].start - segments_[i - 1].end < kGap,
                       touchR = i + 1 < segments_.size() && segments_[i + 1].start - segments_[i].end < kGap;
            const double x0 = xOf(segments_[i].start) + (touchL ? 1 : 0), x1 = xOf(segments_[i].end) - (touchR ? 1 : 0);
            const QRectF r(x0, lane.top(), qMax(2.0, x1 - x0), lane.height());
            QPainterPath frame;
            frame.addRoundedRect(r, kRadius, kRadius);
            if (touchL)
                frame = frame.united(rectPath(QRectF(r.left(), r.top(), kRadius, r.height())));
            if (touchR)
                frame = frame.united(rectPath(QRectF(r.right() - kRadius, r.top(), kRadius, r.height())));
            const bool hotL = hotSeg_ == i && hotSide_ == 0, hotR = hotSeg_ == i && hotSide_ == 1;
            p.setPen(Qt::NoPen);
            p.setBrush(accent);
            p.drawPath(frame);
            p.setBrush(body);
            p.drawRect(QRectF(r.left() + (touchL ? 0 : kHandle), r.top() + kBar, r.width() - (touchL ? 0 : kHandle) - (touchR ? 0 : kHandle),
                              r.height() - 2 * kBar));
            QColor hot = accent.lighter(125);
            QColor grip = onAccent;
            grip.setAlpha(230);
            for (int side = 0; side < 2; ++side) {
                if (side == 0 ? touchL : touchR) {
                    if (side == 1 && touchR) {
                        // Split line between touching clips: a slim grabber with its own grip.
                        const QRectF d(r.right() - 3, r.top(), 6, r.height());
                        p.setBrush(hotR ? hot : accent);
                        p.drawRoundedRect(d, 3, 3);
                        p.setBrush(grip);
                        p.drawRoundedRect(QRectF(d.center().x() - 0.75, d.center().y() - 7, 1.5, 14), 0.75, 0.75);
                    }
                    continue;
                }
                const QRectF h(side == 0 ? r.left() : r.right() - kHandle, r.top(), kHandle, r.height());
                if (side == 0 ? hotL : hotR) {
                    QPainterPath hp;
                    hp.addRoundedRect(h.adjusted(side == 0 ? 0 : -kRadius, 0, side == 0 ? kRadius : 0, 0), kRadius, kRadius);
                    p.setBrush(hot);
                    p.drawPath(hp.intersected(rectPath(h)));
                }
                p.setBrush(grip);
                p.drawRoundedRect(QRectF(h.center().x() - 1.25, h.center().y() - 9, 2.5, 18), 1.25, 1.25);
            }
        }
        // Ghost playhead: where Split Here would cut, shown while hovering a splittable spot.
        if (hoverX_ >= 0 && drag_ == None) {
            const double gx = xOf(secOf(hoverX_));
            QColor key = base, line = text;
            key.setAlpha(110);
            line.setAlpha(150);
            p.setPen(QPen(key, 5.5, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(gx, 5), QPointF(gx, height() - 5));
            p.setPen(QPen(line, 2.5, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(gx, 5), QPointF(gx, height() - 5));
        }
        // Playhead: a text-coloured line with a base-coloured keyline so it reads on any segment.
        const double px = xOf(qBound(0.0, playhead_, duration_));
        p.setPen(QPen(base, 5.5, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(px, 2), QPointF(px, height() - 2));
        p.setPen(QPen(text, 2.5, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(px, 2), QPointF(px, height() - 2));
    }
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() != Qt::LeftButton || duration_ <= 0) {
            QWidget::mousePressEvent(e);
            return;
        }
        const double x = e->position().x();
        hoverX_ = -1;
        const auto hit = nearPlayhead(x) ? Hit{} : edgeAt(x);
        if (hit.seg >= 0) {
            drag_ = Edge;
            dragSeg_ = hit.seg;
            dragSide_ = hit.side;
            // A split line holds two edges. Which one moves is decided by the first drag direction.
            dividerPending_ = hit.side == 1 && touchesNext(hit.seg);
            pressX_ = x;
            dragOffset_ = xOf(edgeValue(hit.seg, hit.side)) - x;
            dragBefore_ = segments_;
            moved_ = false;
        } else {
            drag_ = Playhead;
            playhead_ = secOf(x);
            if (onSeek)
                onSeek(playhead_, false);
            update();
        }
        e->accept();
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        const double x = e->position().x();
        if (drag_ == Playhead) {
            playhead_ = secOf(x);
            if (onSeek)
                onSeek(playhead_, false);
            update();
        } else if (drag_ == Edge) {
            if (dividerPending_) {
                if (std::abs(x - pressX_) < 2)
                    return e->accept();
                // Pull left: the left clip's end moves. Pull right: the right clip's start moves.
                // Either way the split opens into a gap and the other clip stays put.
                if (x > pressX_) {
                    ++dragSeg_;
                    dragSide_ = 0;
                }
                dividerPending_ = false;
                dragOffset_ = xOf(edgeValue(dragSeg_, dragSide_)) - pressX_;
            }
            const double t = clampEdge(secOf(x + dragOffset_));
            if (setEdge(dragSeg_, dragSide_, t)) {
                moved_ = true;
                playhead_ = t;
                if (onSeek)
                    onSeek(t, false);
                if (onChanged)
                    onChanged();
                update();
            }
        } else {
            const auto hit = nearPlayhead(x) ? Hit{} : edgeAt(x);
            if (hit.seg != hotSeg_ || hit.side != hotSide_) {
                hotSeg_ = hit.seg;
                hotSide_ = hit.side;
                update();
            }
            setCursor(hit.seg >= 0 ? Qt::SizeHorCursor : Qt::PointingHandCursor);
            setHover(x);
        }
        e->accept();
    }
    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->button() != Qt::LeftButton || drag_ == None) {
            QWidget::mouseReleaseEvent(e);
            return;
        }
        if (drag_ == Edge && moved_) {
            undo_.append(dragBefore_);
            if (undo_.size() > 100)
                undo_.removeFirst();
        }
        drag_ = None;
        if (onSeek)
            onSeek(playhead_, true);
        update();
        e->accept();
    }
    void leaveEvent(QEvent *e) override {
        if (!menuOpen_ && hoverX_ >= 0) {
            hoverX_ = -1;
            update();
        }
        if (hotSeg_ != -1) {
            hotSeg_ = -1;
            update();
        }
        QWidget::leaveEvent(e);
    }
    void contextMenuEvent(QContextMenuEvent *e) override {
        if (duration_ <= 0 || drag_ != None)
            return;
        const double t = secOf(e->pos().x());
        menuOpen_ = true;
        setHover(e->pos().x());
        QMenu menu(this);
        const bool inClip = indexAt(t) >= 0;
        if (inClip) {
            auto *split = menu.addAction("Split Here");
            split->setEnabled(canSplitAt(t));
            connect(split, &QAction::triggered, this, [this, t] { splitAt(t); });
        } else {
            connect(menu.addAction("Restore"), &QAction::triggered, this, [this, t] { restoreAt(t); });
        }
        const double head = playhead_;
        auto *atHead = menu.addAction("Split at Playhead");
        atHead->setEnabled(canSplitAt(head));
        connect(atHead, &QAction::triggered, this, [this, head] { splitAt(head); });
        if (inClip) {
            auto *del = menu.addAction("Delete");
            del->setEnabled(segments_.size() > 1);
            connect(del, &QAction::triggered, this, [this, t] { removeAt(t); });
        }
        menu.exec(e->globalPos());
        menuOpen_ = false;
        hoverX_ = -1;
        hotSeg_ = -1;
        update();
    }

  private:
    enum Drag { None, Playhead, Edge };
    struct Hit {
        int seg = -1;
        int side = 0; // 0 = start, 1 = end
    };
    static constexpr int kHeight = 48, kPad = 6, kRadius = 8, kHandle = 14, kBar = 3;
    QVector<Segment> segments_, dragBefore_;
    QVector<QVector<Segment>> undo_;
    double duration_ = 0, playhead_ = 0, dragOffset_ = 0;
    Drag drag_ = None;
    int dragSeg_ = 0, dragSide_ = 0, hotSeg_ = -1, hotSide_ = 0;
    bool moved_ = false, dividerPending_ = false;
    double pressX_ = 0;
    double hoverX_ = -1;
    bool menuOpen_ = false;

    static QPainterPath rectPath(const QRectF &r) {
        QPainterPath path;
        path.addRect(r);
        return path;
    }
    // The playhead sits above split lines: grabbing it never drags a split that happens to share its spot.
    bool nearPlayhead(double x) const { return std::abs(x - xOf(qBound(0.0, playhead_, duration_))) <= 6; }
    void setHover(double x) {
        const bool show = x >= 0 && drag_ == None && !nearPlayhead(x) && edgeAt(x).seg < 0 && canSplitAt(secOf(x));
        const double next = show ? x : -1;
        if (next != hoverX_) {
            hoverX_ = next;
            update();
        }
    }
    bool canSplitAt(double t) const {
        const int i = indexAt(t);
        return i >= 0 && t - segments_[i].start >= minLen() && segments_[i].end - t >= minLen();
    }
    double xOf(double t) const { return duration_ > 0 ? t / duration_ * width() : 0; }
    double secOf(double x) const { return width() > 0 ? qBound(0.0, x / width() * duration_, duration_) : 0; }
    // Shortest segment: 0.1 s, but never so short that its two handles collide.
    double minLen() const { return qMax(kMinSeconds, (2.0 * kHandle + 4) / qMax(1, width()) * duration_); }
    void remember() {
        undo_.append(segments_);
        if (undo_.size() > 100)
            undo_.removeFirst();
    }
    void changed() {
        update();
        if (onChanged)
            onChanged();
    }
    bool touchesPrev(int i) const { return i > 0 && segments_[i].start - segments_[i - 1].end < kGap; }
    bool touchesNext(int i) const { return i + 1 < segments_.size() && segments_[i + 1].start - segments_[i].end < kGap; }
    double edgeValue(int seg, int side) const { return side == 0 ? segments_[seg].start : segments_[seg].end; }
    // Closest draggable edge under x. A split line between two kept segments is one shared edge.
    Hit edgeAt(double x) const {
        Hit best;
        double bestDist = 1e9;
        for (int i = 0; i < segments_.size(); ++i) {
            for (int side = 0; side < 2; ++side) {
                if (side == 0 && touchesPrev(i))
                    continue;
                const bool divider = side == 1 && touchesNext(i);
                const double ex = xOf(edgeValue(i, side));
                const double lo = divider ? ex - 5 : (side == 0 ? ex - 4 : ex - kHandle - 2);
                const double hi = divider ? ex + 5 : (side == 0 ? ex + kHandle + 2 : ex + 4);
                if (x >= lo && x <= hi && std::abs(x - ex) < bestDist) {
                    bestDist = std::abs(x - ex);
                    best = {i, side};
                }
            }
        }
        return best;
    }
    double clampEdge(double t) const {
        const int i = dragSeg_;
        if (dragSide_ == 0)
            return qBound(i > 0 ? segments_[i - 1].end : 0.0, t, segments_[i].end - minLen());
        return qBound(segments_[i].start + minLen(), t, i + 1 < segments_.size() ? segments_[i + 1].start : duration_);
    }
    bool setEdge(int seg, int side, double t) {
        if (std::abs(edgeValue(seg, side) - t) < 1e-9)
            return false;
        if (side == 0)
            segments_[seg].start = t;
        else
            segments_[seg].end = t;
        return true;
    }
};
} // namespace mpvideo
