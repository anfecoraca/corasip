#include "ui/hotkey_x11.hpp"

#include <QGuiApplication>
#include <QSocketNotifier>
#include <QStringList>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

namespace corasip::ui {

namespace {

// Errors from the X server while grabbing. XGrabKey returns nothing, so a failed grab shows up as an error event.
bool g_grabFailed = false;

int grabErrorHandler(Display*, XErrorEvent*) {
    g_grabFailed = true;
    return 0;
}

}  // namespace

GlobalHotkey::GlobalHotkey(QObject* parent) : QObject(parent) {}

GlobalHotkey::~GlobalHotkey() { unbind(); }

bool GlobalHotkey::bind(const QString& combo, QString& err) {
    unbind();
    if (QGuiApplication::platformName() != QLatin1String("xcb")) {
        err = QStringLiteral("Global hotkey needs an X11 session. It is off under Wayland.");
        return false;
    }
    display_ = XOpenDisplay(nullptr);
    if (display_ == nullptr) {
        err = QStringLiteral("Global hotkey off: no X display.");
        return false;
    }

    modifiers_ = 0;
    const QStringList parts = combo.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        err = QStringLiteral("Global hotkey: no key given.");
        unbind();
        return false;
    }
    for (int i = 0; i + 1 < parts.size(); ++i) {
        const QString m = parts[i].trimmed().toLower();
        if (m == QLatin1String("ctrl") || m == QLatin1String("control")) modifiers_ |= ControlMask;
        else if (m == QLatin1String("alt")) modifiers_ |= Mod1Mask;
        else if (m == QLatin1String("shift")) modifiers_ |= ShiftMask;
        else if (m == QLatin1String("super") || m == QLatin1String("win")) modifiers_ |= Mod4Mask;
        else {
            err = QStringLiteral("Global hotkey: unknown modifier %1.").arg(parts[i]);
            unbind();
            return false;
        }
    }
    const std::string key = parts.last().trimmed().toLower().toStdString();
    KeySym sym = XStringToKeysym(key.c_str());
    if (sym == NoSymbol) {
        err = QStringLiteral("Global hotkey: unknown key %1.").arg(parts.last());
        unbind();
        return false;
    }
    keycode_ = XKeysymToKeycode(display_, sym);
    if (keycode_ == 0) {
        err = QStringLiteral("Global hotkey: key %1 is not on this keyboard.").arg(parts.last());
        unbind();
        return false;
    }

    // Grab every combination of Caps Lock and Num Lock, so the hotkey works whichever is on.
    g_grabFailed = false;
    XErrorHandler previous = XSetErrorHandler(grabErrorHandler);
    const unsigned int locks[] = {0, LockMask, Mod2Mask, LockMask | Mod2Mask};
    for (unsigned int lock : locks) {
        XGrabKey(display_, keycode_, modifiers_ | lock, DefaultRootWindow(display_), True, GrabModeAsync, GrabModeAsync);
    }
    XSync(display_, False);
    XSetErrorHandler(previous);
    if (g_grabFailed) {
        err = QStringLiteral("Global hotkey %1 is used by another program.").arg(combo);
        unbind();
        return false;
    }

    notifier_ = new QSocketNotifier(ConnectionNumber(display_), QSocketNotifier::Read, this);
    connect(notifier_, &QSocketNotifier::activated, this, &GlobalHotkey::onReadable);
    return true;
}

void GlobalHotkey::unbind() {
    delete notifier_;
    notifier_ = nullptr;
    if (display_ != nullptr) {
        XUngrabKey(display_, keycode_, AnyModifier, DefaultRootWindow(display_));
        XCloseDisplay(display_);
        display_ = nullptr;
    }
    keycode_ = 0;
}

void GlobalHotkey::onReadable() {
    if (display_ == nullptr) return;
    while (XPending(display_) > 0) {
        XEvent ev;
        XNextEvent(display_, &ev);
        if (ev.type != KeyPress || ev.xkey.keycode != static_cast<unsigned int>(keycode_)) continue;
        // Ignore Caps Lock and Num Lock when matching the modifiers.
        const unsigned int state = ev.xkey.state & ~(LockMask | Mod2Mask);
        if (state == modifiers_) emit activated();
    }
}

}  // namespace corasip::ui
