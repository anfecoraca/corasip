// Global hotkey on X11 (M5F). Grabs one key combination on the root window through Xlib.
// Wayland has no global grabs for applications, so the hotkey is off there (SPEC D31).
#pragma once

#include <QObject>
#include <QString>

class QSocketNotifier;
struct _XDisplay;

namespace corasip::ui {

class GlobalHotkey : public QObject {
    Q_OBJECT
public:
    explicit GlobalHotkey(QObject* parent = nullptr);
    ~GlobalHotkey() override;
    GlobalHotkey(const GlobalHotkey&) = delete;
    GlobalHotkey& operator=(const GlobalHotkey&) = delete;

    // Grabs a combination such as "Ctrl+Alt+A". Returns false and sets err when it cannot be used:
    // not an X11 session, an unknown key, or another program already holds the combination.
    bool bind(const QString& combo, QString& err);
    void unbind();

signals:
    void activated();

private:
    void onReadable();

    _XDisplay* display_ = nullptr;
    QSocketNotifier* notifier_ = nullptr;
    unsigned int modifiers_ = 0;
    int keycode_ = 0;
};

}  // namespace corasip::ui
