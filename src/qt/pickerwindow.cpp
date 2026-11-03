#include "pickerwindow.h"

#include "../common/picker.h"
#include "theme.h"

#include <QApplication>
#include <QEventLoop>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QTimer>

#include <algorithm>
#include <memory>
#include <vector>

namespace {

QColor typeColor(ClickType t) {
    switch (t) {
    case ClickType::Right: return theme::kStop;
    case ClickType::Middle: return QColor("#f9e2af");
    case ClickType::Double: return theme::kGo;
    default: return theme::kAccent;
    }
}

QString typeLabel(ClickType t) {
    switch (t) {
    case ClickType::Right: return "Right";
    case ClickType::Middle: return "Middle";
    case ClickType::Double: return "Double";
    default: return "Left";
    }
}

} // namespace

PickerWindow::PickerWindow(PickerState &state, QScreen *screen, bool showsBar)
    : state_(state), showsBar_(showsBar) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setWindowTitle("Catberry quick click: pick points");
    setCursor(Qt::CrossCursor);
    setScreen(screen);
    setGeometry(screen->geometry());
}

void PickerWindow::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(30, 30, 46, 110));
    // Desktop pixels to this window: the desktop origin, then this screen's place on it.
    QPoint offset = state_.origin - geometry().topLeft();
    auto at = [&](const QuickPoint &q) { return QPointF(q.x + offset.x(), q.y + offset.y()); };

    p.setPen(QPen(QColor(205, 214, 244, 160), 2, Qt::DashLine));
    for (size_t i = 1; i < state_.points.size(); ++i) p.drawLine(at(state_.points[i - 1]), at(state_.points[i]));

    QFont f = font();
    f.setBold(true);
    f.setPointSize(10);
    p.setFont(f);
    for (size_t i = 0; i < state_.points.size(); ++i) {
        QPointF c = at(state_.points[i]);
        p.setPen(QPen(theme::kBg, 2));
        p.setBrush(typeColor(state_.points[i].type));
        p.drawEllipse(c, 13, 13);
        p.setPen(theme::kBg);
        p.drawText(QRectF(c.x() - 13, c.y() - 13, 26, 26), Qt::AlignCenter, QString::number(i + 1));
        if (state_.points[i].type == ClickType::Double) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(typeColor(ClickType::Double), 2));
            p.drawEllipse(c, 18, 18);
        }
    }

    if (!showsBar_) return;
    QString text = QString("Click: add a point (%1)  ·  L / R / M / D: click type  ·  Backspace: undo  ·  "
                           "Enter: save (%2)  ·  Esc: cancel")
                       .arg(typeLabel(state_.type))
                       .arg(state_.points.size());
    QFont bar = font();
    bar.setPointSize(11);
    p.setFont(bar);
    // One line when it fits, otherwise wrapped onto more.
    int maxWidth = std::max(200, width() - 40);
    QRect need = QFontMetrics(bar).boundingRect(QRect(0, 0, maxWidth - 40, 1000), Qt::TextWordWrap, text);
    QRectF box(0, 0, need.width() + 40, need.height() + 20);
    box.moveTopLeft(QPointF((width() - box.width()) / 2.0, 16));
    p.setPen(QPen(typeColor(state_.type), 2));
    p.setBrush(theme::kPanel);
    p.drawRoundedRect(box, 8, 8);
    p.setPen(theme::kText);
    p.drawText(box.adjusted(20, 10, -20, -10), Qt::AlignCenter | Qt::TextWordWrap, text);
}

void PickerWindow::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) return;
    QPoint global = geometry().topLeft() + event->position().toPoint();
    state_.points.push_back({global.x() - state_.origin.x(), global.y() - state_.origin.y(), state_.type});
    state_.changed();
}

void PickerWindow::keyPressEvent(QKeyEvent *event) {
    switch (event->key()) {
    case Qt::Key_L: state_.type = ClickType::Left; break;
    case Qt::Key_R: state_.type = ClickType::Right; break;
    case Qt::Key_M: state_.type = ClickType::Middle; break;
    case Qt::Key_D: state_.type = ClickType::Double; break;
    case Qt::Key_Backspace:
    case Qt::Key_Delete:
        if (!state_.points.empty()) state_.points.pop_back();
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (!state_.points.empty()) return state_.finish(true);
        break;
    case Qt::Key_Escape: return state_.finish(false);
    default: return QWidget::keyPressEvent(event);
    }
    state_.changed();
}

bool pickPoints(QuickSet &set) {
    // From the command line (-quickpick) there is no application yet.
    std::unique_ptr<QApplication> own;
    static int argc = 1;
    static char name[] = "catberry-mtools";
    static char *argv[] = {name, nullptr};
    if (!qApp) {
        own.reset(new QApplication(argc, argv));
        own->setDesktopFileName("catberry-mtools");
    }

    // The desktop: all screens together.
    QRect desktop;
    for (QScreen *s : QGuiApplication::screens()) desktop = desktop.united(s->geometry());
    PickerState state;
    state.origin = desktop.topLeft();
    // Points of a set picked on another desktop size are shown where they will click.
    for (const QuickPoint &p : set.points)
        state.points.push_back(scaledPoint(p, set.desktopW, set.desktopH, desktop.width(), desktop.height()));

    QEventLoop loop;
    bool saved = false;
    std::vector<std::unique_ptr<PickerWindow>> windows;
    state.finish = [&](bool ok) {
        saved = ok;
        loop.quit();
    };
    state.changed = [&] {
        for (auto &w : windows) w->update();
    };
    QScreen *primary = QGuiApplication::primaryScreen();
    for (QScreen *s : QGuiApplication::screens()) {
        windows.emplace_back(new PickerWindow(state, s, s == primary));
        windows.back()->showFullScreen();
    }
    if (!windows.empty()) {
        windows.front()->activateWindow();
        windows.front()->setFocus();
    }

    // Tests: after CATBERRY_PICK_QUIT_MS, a picture of the picker (with
    // CATBERRY_SCREENSHOT), then cancel; or, with CATBERRY_PICK_ADD="x,y",
    // add that point and save.
    int quitMs = qEnvironmentVariableIntValue("CATBERRY_PICK_QUIT_MS");
    if (quitMs > 0)
        QTimer::singleShot(quitMs, &loop, [&] {
            QString shot = qEnvironmentVariable("CATBERRY_SCREENSHOT");
            if (!shot.isEmpty() && !windows.empty()) windows.front()->grab().save(shot);
            QStringList add = qEnvironmentVariable("CATBERRY_PICK_ADD").split(',');
            if (add.size() == 2) {
                state.points.push_back({add[0].toInt(), add[1].toInt(), state.type});
                state.finish(true);
            } else {
                state.finish(false);
            }
        });
    loop.exec();
    for (auto &w : windows) w->close();

    if (!saved) return false;
    set.points = state.points;
    set.desktopW = desktop.width();
    set.desktopH = desktop.height();
    return true;
}
