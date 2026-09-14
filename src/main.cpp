#include <QApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>

#include "mainwindow.h"
#include "settings.h"

// Mirrors SettingsModel::configDir selection so the persisted theme can be
// applied via the platform theme before the QApplication exists.
static QString appConfigDir() {
    const QByteArray xdg = qgetenv("XDG_CONFIG_HOME");
    if (!xdg.isEmpty()) return QString::fromLocal8Bit(xdg) + "/hypr";
    return QDir::homePath() + "/.config/hypr";
}

int main(int argc, char *argv[]) {
    // Apply the persisted theme (GTK / KDE Plasma platform theme, icon theme)
    // before the QApplication exists.
    QFile prefs(appConfigDir() + "/hyprset-preferences.conf");
    if (prefs.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString style, icons;
        for (const QString &raw : QString::fromUtf8(prefs.readAll()).split('\n')) {
            const QString line = raw.trimmed();
            if (line.startsWith("style=")) style = line.mid(6).trimmed();
            else if (line.startsWith("icons=")) icons = line.mid(6).trimmed();
            else if (style.isEmpty()) style = line; // legacy single-line value
        }
        if (style == "GTK3") qputenv("QT_QPA_PLATFORMTHEME", "gtk3");
        else if (style == "Breeze") qputenv("QT_QPA_PLATFORMTHEME", "kde");
        if (!icons.isEmpty()) qputenv("QT_ICON_THEME", icons.toUtf8());
    }

    QApplication app(argc, argv);
    app.setApplicationName("HyprSet");

    for (int i = 1; i < argc; ++i) {
        if (QString(argv[i]) == "--dump") {
            SettingsModel model;
            model.load();
            QTextStream out(stdout);
            const auto specs = SettingSpec::all();
            for (const SettingSpec &s : specs) {
                QVariant v = model.valueAt(s.key);
                out << s.key << " = " << (v.isValid() ? v.toString() : QString("<missing>")) << "\n";
            }
            out << "idle lock=" << model.idleLockSec << " dpms=" << model.idleDpmsSec
                << " suspend=" << model.idleSuspendSec << "\n";
            for (const AppField &f : appFields()) {
                out << "app " << f.var << " = " << model.overrideVar(f.var).toString()
                    << " (default: " << model.defaultVar(f.var).toString()
                    << ") [key: " << model.keybindForApp(f.var) << "]\n";
            }
            out << "keybinds=" << model.keybinds().size() << "\n";
            for (const KeyBind &b : model.keybinds().mid(0, 12))
                out << "  bind " << b.display << "  ->  " << b.description << "\n";
            return 0;
        }
        if (QString(argv[i]) == "--export" && i + 1 < argc) {
            SettingsModel model;
            model.load();
            QString err;
            if (!SettingsModel::exportSnapshot(argv[i + 1], model.configPath(), &err)) {
                QTextStream out(stderr);
                out << "Export failed: " << err << "\n";
                return 1;
            }
            QTextStream out(stdout);
            out << "Exported config snapshot to " << argv[i + 1] << "\n";
            return 0;
        }
    }

    MainWindow w;
    w.show();
    return app.exec();
}