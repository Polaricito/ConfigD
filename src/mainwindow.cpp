#include "mainwindow.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFile>          // raw-edit tab reads with QFile
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMetaType>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleFactory>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVector>

// A small button that opens a color dialog and paints a swatch.
class ColorButton : public QToolButton {
    Q_OBJECT
public:
    explicit ColorButton(QWidget *parent = nullptr) : QToolButton(parent) {
        setFixedSize(96, 26);
        connect(this, &QToolButton::clicked, this, &ColorButton::pick);
        setColor(QColor(128, 128, 128));
    }
    QColor color() const { return m_color; }
    void setColor(const QColor &c) {
        m_color = c.isValid() ? c : QColor(0, 0, 0);
        QPixmap px(20, 16);
        px.fill(m_color);
        setIcon(QIcon(px));
        setText(m_color.name(QColor::HexArgb));
    }

private slots:
    void pick() {
        QColor next = QColorDialog::getColor(m_color, this, "Border color",
                                             QColorDialog::ShowAlphaChannel);
        if (next.isValid()) setColor(next);
    }

private:
    QColor m_color;
};

// Row data roles for the keybind table.
enum BindRole {
    BindDescRole = Qt::UserRole,
    BindStockRole,  // stock chord ("" for pure-custom binds)
    BindActionRole, // verbatim action source
    BindChordRole,  // effective raw chord
    BindEditableRole,
    BindCustomRole, // row comes from / was remapped in the custom file
    BindOptionsRole,// verbatim options text
};

// Forward decls (defined below) so lambdas above their definition can use them.
static void setWidgetValue(const Bound &b, const QVariant &v);
static QVariant widgetValue(const Bound &b);

static QString logoFilePath() {
    return QDir::homePath() + "/Downloads/logo.png";
}

// Read one key=value line from the preferences file (legacy bare lines count
// as the style).
static QString prefsValue(const QString &path, const QString &key) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
    QString firstLine;
    for (const QString &raw : QString::fromUtf8(f.readAll()).split('\n')) {
        const QString line = raw.trimmed();
        if (firstLine.isEmpty()) firstLine = line;
        if (line.startsWith(key + "=")) return line.mid(key.length() + 1).trimmed();
    }
    if (key == "style") return firstLine; // backwards compatible
    return QString();
}

static void writePrefs(const QString &path, const QString &style, const QString &icons) {
    if (style.isEmpty() && icons.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    f.write(("style=" + style + "\n").toUtf8());
    f.write(("icons=" + icons + "\n").toUtf8());
    f.close();
}

// Installed icon themes (directories containing index.theme) from the standard
// locations, including the user's ~/.local/share/icons.
static QStringList availableIconThemes() {
    const QStringList roots = {
        "/usr/share/icons",
        "/usr/local/share/icons",
        QDir::homePath() + "/.local/share/icons",
    };
    QStringList themes;
    for (const QString &root : roots) {
        const QDir d(root);
        if (!d.exists()) continue;
        for (const QFileInfo &fi : d.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (QFileInfo::exists(fi.filePath() + "/index.theme")) themes << fi.fileName();
        }
    }
    themes.removeDuplicates();
    themes.sort(Qt::CaseInsensitive);
    return themes;
}

// Translate a QKeyEvent into a raw Hyprland chord ("SUPER + CTRL + Z").
static QString eventToChord(QKeyEvent *e) {
    QString mods;
    const Qt::KeyboardModifiers m = e->modifiers();
    // Only count pressed (not released) modifier state; for a key release,
    // QKeyEvent reports the state *without* that key, so use text modifiers.
    if (m.testFlag(Qt::ControlModifier)) mods += "CTRL + ";
    if (m.testFlag(Qt::AltModifier)) mods += "ALT + ";
    if (m.testFlag(Qt::ShiftModifier)) mods += "SHIFT + ";
    if (m.testFlag(Qt::MetaModifier)) mods += "SUPER + ";

    const int k = e->key();
    QString base;
    switch (k) {
    case Qt::Key_Return: base = "Return"; break;
    case Qt::Key_Enter:  base = "Return"; break;
    case Qt::Key_Tab:    base = "Tab"; break;
    case Qt::Key_Space:  base = "Space"; break;
    case Qt::Key_Escape: base = "Escape"; break;
    case Qt::Key_Left:   base = "Left"; break;
    case Qt::Key_Right:  base = "Right"; break;
    case Qt::Key_Up:     base = "Up"; break;
    case Qt::Key_Down:   base = "Down"; break;
    case Qt::Key_Delete: base = "Delete"; break;
    case Qt::Key_Backspace: base = "Backspace"; break;
    case Qt::Key_Insert: base = "Insert"; break;
    case Qt::Key_Home:   base = "Home"; break;
    case Qt::Key_End:    base = "End"; break;
    case Qt::Key_PageUp:   base = "Page_Up"; break;
    case Qt::Key_PageDown: base = "Page_Down"; break;
    case Qt::Key_Print:  base = "Print"; break;
    case Qt::Key_Backslash: base = "Backslash"; break;
    case Qt::Key_Slash:     base = "Slash"; break;
    case Qt::Key_Question:  base = "Slash"; break; // SHIFT+/
    case Qt::Key_Semicolon: base = "Semicolon"; break;
    case Qt::Key_Colon:     base = "Semicolon"; break;
    case Qt::Key_Apostrophe: base = "Apostrophe"; break;
    case Qt::Key_QuoteDbl:  base = "Apostrophe"; break;
    case Qt::Key_QuoteLeft:  base = "Backtick"; break;
    case Qt::Key_AsciiTilde: base = "Backtick"; break;
    case Qt::Key_Minus:    base = "Minus"; break;
    case Qt::Key_Underscore: base = "Minus"; break;
    case Qt::Key_Equal:    base = "Equal"; break;
    case Qt::Key_Plus:     base = "Equal"; break;
    case Qt::Key_Comma:    base = "Comma"; break;
    case Qt::Key_Less:     base = "Comma"; break;
    case Qt::Key_Period:   base = "Period"; break;
    case Qt::Key_Greater:  base = "Period"; break;
    default:
        if (k >= Qt::Key_F1 && k <= Qt::Key_F35) base = QString("F%1").arg(k - Qt::Key_F1 + 1);
        else if (k >= Qt::Key_0 && k <= Qt::Key_9) base = QString(QChar('0' + (k - Qt::Key_0)));
        else if (k >= Qt::Key_A && k <= Qt::Key_Z) base = QString(QChar('A' + (k - Qt::Key_A)));
        else return QString(); // unhandled (incl. plain modifier presses)
    }
    return mods + base;
}

// A dialog that lets the user type a chord or record it by pressing keys.
class BindEditDialog : public QDialog {
    Q_OBJECT
public:
    explicit BindEditDialog(const QString &titleLabel, const QString &startChord,
                            QWidget *parent = nullptr)
        : QDialog(parent) {
        setWindowTitle(titleLabel);
        auto *v = new QVBoxLayout(this);

        auto *row = new QHBoxLayout;
        m_edit = new QLineEdit;
        m_edit->setPlaceholderText("SUPER + Z");
        auto *record = new QToolButton;
        record->setText("Record…");
        record->setToolTip("Click, then press the new key combination.");
        connect(record, &QToolButton::clicked, this, &BindEditDialog::startRecording);
        row->addWidget(m_edit, 1);
        row->addWidget(record);
        v->addLayout(row);

        m_preview = new QLabel;
        m_preview->setStyleSheet("color: palette(mid);");
        v->addWidget(m_preview);

        auto *buttons = new QHBoxLayout;
        auto *ok = new QPushButton("OK");
        ok->setDefault(true);
        auto *cancel = new QPushButton("Cancel");
        connect(ok, &QPushButton::clicked, this, &QDialog::accept);
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        buttons->addStretch(1);
        buttons->addWidget(ok);
        buttons->addWidget(cancel);
        v->addLayout(buttons);

        m_edit->setText(startChord);
        connect(m_edit, &QLineEdit::textChanged, this, &BindEditDialog::updatePreview);
        updatePreview();
    }

    QString chord() const { return m_edit->text().trimmed(); }

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override {
        if (m_recording && ev->type() == QEvent::KeyPress) {
            auto *ke = static_cast<QKeyEvent *>(ev);
            const QString chord = eventToChord(ke);
            if (!chord.isEmpty()) {
                m_edit->setText(chord);
                m_recording = false;
                setRecordingUi(false);
                return true;
            }
            return true; // swallow modifier-only presses while recording
        }
        return QDialog::eventFilter(obj, ev);
    }
    void keyPressEvent(QKeyEvent *e) override {
        if (m_recording) {
            const QString chord = eventToChord(e);
            if (!chord.isEmpty()) {
                m_edit->setText(chord);
                m_recording = false;
                setRecordingUi(false);
            }
            e->accept();
            return;
        }
        QDialog::keyPressEvent(e);
    }

private slots:
    void startRecording() {
        m_recording = true;
        setRecordingUi(true);
        m_edit->setFocus();
        installEventFilter(this);
    }
    void updatePreview() {
        const QString ch = m_edit->text().trimmed();
        m_preview->setText(ch.isEmpty() ? QString() : formatChord(ch));
        setWindowTitle(windowTitle()); // keep title stable
    }

private:
    void setRecordingUi(bool on) {
        m_edit->setStyleSheet(on ? "border: 2px solid palette(highlight);" : QString());
        m_edit->setPlaceholderText(on ? "Press the key combination…" : "SUPER + Z");
    }
    QLineEdit *m_edit = nullptr;
    QLabel *m_preview = nullptr;
    bool m_recording = false;
};

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    m_systemStyleName = qApp->style()->objectName();
    m_systemIconTheme = QIcon::themeName();
    // Make the user's icon themes (and our own installed icon) visible to Qt.
    const QString userIcons = QDir::homePath() + "/.local/share/icons";
    QStringList iconPaths = QIcon::themeSearchPaths();
    if (!iconPaths.contains(userIcons)) {
        iconPaths.removeAll(userIcons);
        iconPaths.prepend(userIcons);
        QIcon::setThemeSearchPaths(iconPaths);
    }

    const QString logoPath = QDir::homePath() + "/Downloads/logo.png";
    if (QFileInfo::exists(logoPath)) setWindowIcon(QIcon(logoPath));
    installDesktopIntegration();

    m_model = new SettingsModel;
    m_model->load();
    buildUi();
    loadValues();

    // Re-apply the last picked theme + icon theme, if any.
    const QString prefsPath = m_model->configPath() + "/hyprset-preferences.conf";
    const QString savedStyle = prefsValue(prefsPath, "style");
    const QString savedIcons = prefsValue(prefsPath, "icons");
    if (!savedStyle.isEmpty()) {
        int idx = m_themeCombo->findData(savedStyle);
        if (idx >= 0)
            m_themeCombo->setCurrentIndex(idx);
    }
    applyStyle(m_themeCombo->currentData().toString());
    if (!savedIcons.isEmpty()) {
        int idx = m_iconThemeCombo->findData(savedIcons);
        if (idx >= 0)
            m_iconThemeCombo->setCurrentIndex(idx);
    }
    applyIconTheme(m_iconThemeCombo->currentIndex());
}

static QWidget *makePage(QWidget *content) {
    QScrollArea *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    return scroll;
}

static QString labelHint(const SettingSpec &s) {
    if (s.hint.isEmpty()) return s.label;
    return s.label + "  (" + s.hint + ")";
}

void MainWindow::buildUi() {
    setWindowTitle("Hyprland settings");
    resize(820, 560);

    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(12, 12, 12, 12);

    auto *header = new QHBoxLayout;
    const QString logoPath = QDir::homePath() + "/Downloads/logo.png";
    if (QFileInfo::exists(logoPath)) {
        auto *logo = new QLabel;
        logo->setPixmap(QIcon(logoPath).pixmap(32, 32));
        logo->setToolTip("HyprSet");
        header->addWidget(logo);
    }
    auto *title = new QLabel(QString("Hyprland settings — editing %1").arg(m_model->configPath()));
    title->setTextInteractionFlags(Qt::TextSelectableByMouse);
    title->setStyleSheet("font-weight: 600; font-size: 14px;");
    header->addWidget(title);
    header->addStretch(1);

    header->addWidget(new QLabel("Theme:"));
    m_themeCombo = new QComboBox;
    m_themeCombo->addItem("System", "System");
    m_themeCombo->addItem("Plain Qt (Fusion)", "Fusion");
    m_themeCombo->addItem("GTK", "GTK3");
    m_themeCombo->addItem("KDE Plasma (Breeze)", "Breeze");
    connect(m_themeCombo, &QComboBox::currentIndexChanged, this,
            [this](int) { applyStyle(m_themeCombo->currentData().toString()); });
    header->addWidget(m_themeCombo);

    header->addSpacing(8);
    header->addWidget(new QLabel("Icons:"));
    m_iconThemeCombo = new QComboBox;
    m_iconThemeCombo->addItem("System", QString());
    for (const QString &t : availableIconThemes())
        m_iconThemeCombo->addItem(t, t);
    connect(m_iconThemeCombo, &QComboBox::currentIndexChanged, this,
            &MainWindow::applyIconTheme);
    header->addWidget(m_iconThemeCombo);
    root->addLayout(header);

    m_globalSearch = new QLineEdit;
    m_globalSearch->setPlaceholderText("Find a setting across the tab pages…");
    m_globalSearch->setClearButtonEnabled(true);
    root->addWidget(m_globalSearch);
    connect(m_globalSearch, &QLineEdit::textChanged, this, &MainWindow::filterSpecRows);

    auto *body = new QHBoxLayout;
    m_nav = new QListWidget;
    m_nav->setFixedWidth(200);
    m_nav->addItem("Appearance");
    m_nav->addItem("Input");
    m_nav->addItem("Colors");
    m_nav->addItem("Layout");
    m_nav->addItem("Idle");
    m_nav->addItem("Applications");
    m_nav->addItem("Keybinds");
    m_nav->addItem("Custom files");
    const QStringList themed = {
        "video-display",                // Appearance
        "input-keyboard",               // Input
        "preferences-color",            // Colors
        "preferences-system-windows",   // Layout
        "system-lock-screen",           // Idle
        "preferences-system-applications", // Applications
        "input-keyboard",               // Keybinds
        "text-x-script",                // Custom files
    };
    const QStyle::StandardPixmap fallbacks[] = {
        QStyle::SP_DesktopIcon,
        QStyle::SP_FileDialogListView,
        QStyle::SP_FileDialogContentsView,
        QStyle::SP_FileDialogDetailedView,
        QStyle::SP_DialogResetButton,
        QStyle::SP_FileIcon,
        QStyle::SP_FileDialogDetailedView,
        QStyle::SP_FileIcon,
    };
    for (int i = 0; i < m_nav->count() && i < themed.size(); ++i) {
        m_nav->item(i)->setIcon(
            QIcon::fromTheme(themed.at(i), style()->standardIcon(fallbacks[i])));
    }
    body->addWidget(m_nav);

    m_stack = new QStackedWidget;
    m_stack->addWidget(makePage(buildSpecPage("display")));
    m_stack->addWidget(makePage(buildSpecPage("input")));
    m_stack->addWidget(makePage(buildSpecPage("colors")));
    m_stack->addWidget(makePage(buildSpecPage("layout")));
    m_stack->addWidget(makePage(buildIdlePage()));
    m_stack->addWidget(makePage(buildAppsPage()));
    m_stack->addWidget(makePage(buildKeybindsPage()));
    m_stack->addWidget(makePage(buildCustomFilesPage()));
    body->addWidget(m_stack, 1);
    root->addLayout(body, 1);

    connect(m_nav, &QListWidget::currentRowChanged, m_stack, &QStackedWidget::setCurrentIndex);
    m_nav->setCurrentRow(0);

    auto *bottom = new QHBoxLayout;
    m_status = new QLabel(QString("Using config folder %1").arg(m_model->configPath()));
    m_status->setWordWrap(true);
    bottom->addWidget(m_status, 1);

    auto *reloadBtn = new QPushButton("Reload config");
    connect(reloadBtn, &QPushButton::clicked, this, [this] {
        QProcess::startDetached("hyprctl", {"reload"});
        m_status->setText("Sent hyprctl reload.");
    });
    bottom->addWidget(reloadBtn);

    auto *applyBtn = new QPushButton("Apply changes");
    applyBtn->setDefault(true);
    applyBtn->setStyleSheet("font-weight: 600;");
    connect(applyBtn, &QPushButton::clicked, this, &MainWindow::saveValues);
    bottom->addWidget(applyBtn);
    root->addLayout(bottom);
    setCentralWidget(central);
}

QWidget *MainWindow::buildSpecPage(const QString &section) {
    auto *box = new QGroupBox;
    auto *form = new QFormLayout(box);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setHorizontalSpacing(16);

    const auto specs = SettingSpec::all();
    for (const SettingSpec &s : specs) {
        if (s.section != section) continue;
        QWidget *w = nullptr;
        switch (s.type) {
        case SettingSpec::Bool: {
            auto *cb = new QCheckBox(box);
            w = cb;
            break;
        }
        case SettingSpec::Int: {
            auto *spin = new QSpinBox(box);
            spin->setRange(int(s.minV), int(s.maxV));
            spin->setSingleStep(int(qMax(1.0, s.stepV)));
            w = spin;
            break;
        }
        case SettingSpec::Double: {
            auto *dspin = new QDoubleSpinBox(box);
            dspin->setRange(s.minV, s.maxV);
            dspin->setSingleStep(s.stepV);
            dspin->setDecimals(s.decimals);
            w = dspin;
            break;
        }
        case SettingSpec::String: {
            auto *le = new QLineEdit(box);
            w = le;
            break;
        }
        case SettingSpec::Color:
            w = new ColorButton(box);
            break;
        case SettingSpec::Combo: {
            auto *combo = new QComboBox(box);
            for (int i = 0; i < s.comboLabels.size(); ++i) {
                combo->addItem(s.comboLabels.at(i), s.comboData.at(i));
            }
            w = combo;
            break;
        }
        }

        auto *reset = new QToolButton(box);
        reset->setIcon(style()->standardIcon(QStyle::SP_DialogResetButton));
        reset->setToolTip("Reset this setting to its stock default");
        reset->setAutoRaise(true);
        Bound b{s, w};
        connect(reset, &QToolButton::clicked, this, [this, b] {
            setWidgetValue(b, m_model->defaultValue(b.spec.key));
        });

        auto *cell = new QWidget(box);
        auto *h = new QHBoxLayout(cell);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(4);
        h->addWidget(w, 1);
        h->addWidget(reset);

        auto *label = new QLabel(labelHint(s), box);
        form->addRow(label, cell);
        m_specRowWidgets.insert(s.key, {label, w});
        m_generalBindings.append(b);
    }
    return box;
}

void MainWindow::filterSpecRows(const QString &query) {
    const QString q = query.trimmed();
    for (auto it = m_specRowWidgets.constBegin(); it != m_specRowWidgets.constEnd(); ++it) {
        const bool visible = q.isEmpty() || it.key().contains(q, Qt::CaseInsensitive) ||
                             (it.value().first && it.value().first->text().contains(q, Qt::CaseInsensitive));
        if (it.value().first) it.value().first->setVisible(visible);
        if (it.value().second) it.value().second->setVisible(visible);
    }
}

void MainWindow::applyStyle(const QString &name) {
    if (m_themeCombo) {
        QFile prefs(m_model->configPath() + "/hyprset-preferences.conf");
        if (prefs.open(QIODevice::WriteOnly | QIODevice::Text)) {
            prefs.write(name.toUtf8());
            prefs.close();
        }
    }

    QString style = name;
    if (style.isEmpty() || style == "System") style = m_systemStyleName;
    QStyle *chosen = QStyleFactory::create(style);
    if (chosen) qApp->setStyle(chosen);

    if (style == "GTK3")
        m_status->setText("GTK theme saved — applies on the next launch (qt6-gtk platform theme).");
    else if (style == "Breeze")
        m_status->setText("Theme: KDE Plasma (Breeze)");
    else
        m_status->setText("Theme: " + style);
}

void MainWindow::applyIconTheme(int) {
    const QString name = m_iconThemeCombo->currentData().toString();
    QIcon::setThemeName(name.isEmpty() ? m_systemIconTheme : name);
    writePrefs(m_model->configPath() + "/hyprset-preferences.conf",
               m_themeCombo->currentData().toString(), name);
    if (m_status)
        m_status->setText(name.isEmpty() ? QString("Icons: system (%1)").arg(m_systemIconTheme)
                                         : QString("Icons: %1").arg(name));
}

void MainWindow::installDesktopIntegration() {
    // Make the app visible to docks/taskbars: a per-user .desktop entry plus
    // the icon installed into the user's icon theme directory.
    const QString logo = logoFilePath();
    if (!QFileInfo::exists(logo)) return;

    const QString iconFile =
        QDir::homePath() + "/.local/share/icons/hicolor/128x128/apps/hyprset.png";
    QDir().mkpath(QFileInfo(iconFile).absolutePath());
    QFile::remove(iconFile);
    QFile::copy(logo, iconFile);

    const QString desktopPath = QDir::homePath() + "/.local/share/applications/hyprset.desktop";
    QDir().mkpath(QFileInfo(desktopPath).absolutePath());
    const QString binPath = QCoreApplication::applicationFilePath();
    QFile df(desktopPath);
    if (df.open(QIODevice::WriteOnly | QIODevice::Text)) {
        df.write(QString(
                     "[Desktop Entry]\n"
                     "Type=Application\n"
                     "Name=HyprSet\n"
                     "Comment=Hyprland settings editor\n"
                     "Exec=%1\n"
                     "Icon=hyprset\n"
                     "Terminal=false\n"
                     "Categories=Settings;Utility;\n"
                     "StartupWMClass=hyprset\n")
                     .arg(binPath)
                     .toUtf8());
        df.close();
    }
}

void MainWindow::showChangesDialog(const QString &summary) {
    if (!m_changesDialog) {
        m_changesDialog = new QDialog(this);
        m_changesDialog->setAttribute(Qt::WA_DeleteOnClose);
        m_changesDialog->setWindowTitle("Applied changes");
        m_changesDialog->resize(560, 380);
        auto *lay = new QVBoxLayout(m_changesDialog);
        m_changesTxt = new QPlainTextEdit(m_changesDialog);
        m_changesTxt->setReadOnly(true);
        m_changesTxt->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        lay->addWidget(m_changesTxt);
        auto *btn = new QPushButton("OK");
        connect(btn, &QPushButton::clicked, m_changesDialog, &QDialog::accept);
        auto *hl = new QHBoxLayout;
        hl->addStretch(1);
        hl->addWidget(btn);
        lay->addLayout(hl);
        connect(m_changesDialog, &QObject::destroyed, this, [this] { m_changesDialog = nullptr; });
    }
    m_changesTxt->setPlainText(summary);
    m_changesDialog->show();
    m_changesDialog->raise();
    m_changesDialog->activateWindow();
}

QWidget *MainWindow::buildIdlePage() {
    auto *box = new QGroupBox("Inactivity (hypridle)");
    auto *form = new QFormLayout(box);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setHorizontalSpacing(16);

    m_lockMin = new QSpinBox(box);
    m_lockMin->setRange(0, 600);
    m_lockMin->setSuffix(" min");
    m_dpmsMin = new QSpinBox(box);
    m_dpmsMin->setRange(0, 600);
    m_dpmsMin->setSuffix(" min");
    m_suspendMin = new QSpinBox(box);
    m_suspendMin->setRange(0, 600);
    m_suspendMin->setSuffix(" min");

    form->addRow("Lock screen after", m_lockMin);
    form->addRow("Turn display off after", m_dpmsMin);
    form->addRow("Suspend after", m_suspendMin);
    return box;
}

QWidget *MainWindow::buildAppsPage() {
    auto *box = new QGroupBox("Default applications (variables.lua)");
    auto *form = new QFormLayout(box);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setHorizontalSpacing(16);

    const auto fields = appFields();
    for (const AppField &f : fields) {
        auto *cell = new QWidget(box);
        auto *v = new QVBoxLayout(cell);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(1);

        auto *le = new QLineEdit(cell);
        le->setClearButtonEnabled(true);
        v->addWidget(le);
        m_appFields.insert(f.var, le);

        auto *hint = new QLabel(cell);
        hint->setStyleSheet("color: palette(mid); font-size: 11px;");
        hint->setTextInteractionFlags(Qt::TextSelectableByMouse);
        v->addWidget(hint);
        m_appKeyHints.insert(f.var, hint);

        form->addRow(f.label, cell);
    }
    return box;
}

QWidget *MainWindow::buildCustomFilesPage() {
    auto *page = new QWidget;
    auto *v = new QVBoxLayout(page);

    auto *pickerRow = new QHBoxLayout;
    m_filePicker = new QComboBox;
    const QString base = m_model->configPath();
    const QStringList candidates = {
        base + "/custom/general.lua",
        base + "/custom/variables.lua",
        base + "/custom/keybinds.lua",
        base + "/custom/env.lua",
        base + "/custom/execs.lua",
        base + "/custom/rules.lua",
        base + "/hypridle.conf",
    };
    for (const QString &path : candidates) {
        if (QFileInfo::exists(path))
            m_filePicker->addItem(QFileInfo(path).fileName() + "  (" + QFileInfo(path).absolutePath() + ")",
                                  path);
    }
    auto *unmanaged = new QLabel("Hand-edit the update-friendly custom/ files");
    unmanaged->setStyleSheet("color: palette(mid); font-size: 11px;");
    pickerRow->addWidget(m_filePicker, 1);
    pickerRow->addWidget(unmanaged);
    v->addLayout(pickerRow);

    m_fileEditor = new QPlainTextEdit;
    m_fileEditor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_fileEditor->setLineWrapMode(QPlainTextEdit::NoWrap);
    v->addWidget(m_fileEditor, 1);

    auto *buttons = new QHBoxLayout;
    auto *reloadBtn = new QPushButton("Reload from disk");
    auto *validateBtn = new QPushButton("Validate (luac)");
    auto *saveBtn = new QPushButton("Save file");
    saveBtn->setStyleSheet("font-weight: 600;");
    connect(reloadBtn, &QPushButton::clicked, this, &MainWindow::refreshCustomEditors);
    connect(validateBtn, &QPushButton::clicked, this, [this] {
        const bool ok = SettingsModel::validateLuaSource(m_fileEditor->toPlainText());
        m_fileState->setText(ok ? "Valid Lua, ready to save."
                                : "Lua syntax error — fix it before saving.");
    });
    connect(saveBtn, &QPushButton::clicked, this, [this] {
        const QString path = m_filePicker->currentData().toString();
        if (path.isEmpty()) return;
        const bool isLua = path.endsWith(".lua");
        if (isLua && !SettingsModel::validateLuaSource(m_fileEditor->toPlainText())) {
            QMessageBox::warning(this, "Save blocked",
                                 "The file has Lua syntax errors. Fix them or close the tab "
                                 "without saving.");
            return;
        }
        QFile wf(path);
        if (wf.open(QIODevice::WriteOnly | QIODevice::Text)) {
            wf.write(m_fileEditor->toPlainText().toUtf8());
            wf.close();
            m_model->reloadAfterCustomEdit();
            m_model->commitConfigChanges();
            m_fileState->setText("Saved. hyprctl reload requested.");
        } else {
            m_fileState->setText("Could not write " + path);
        }
    });
    buttons->addWidget(reloadBtn);
    buttons->addWidget(validateBtn);
    buttons->addStretch(1);
    buttons->addWidget(saveBtn);
    v->addLayout(buttons);

    m_fileState = new QLabel;
    m_fileState->setStyleSheet("color: palette(mid);");
    v->addWidget(m_fileState);

    connect(m_filePicker, &QComboBox::currentIndexChanged, this, &MainWindow::refreshCustomEditors);
    refreshCustomEditors();
    return page;
}

void MainWindow::refreshCustomEditors() {
    const QString path = m_filePicker->currentData().toString();
    if (path.isEmpty()) {
        m_fileEditor->clear();
        m_fileState->setText("No editable custom files found.");
        return;
    }
    QFile f(path);
    m_fileEditor->setPlainText(f.open(QIODevice::ReadOnly | QIODevice::Text)
                                   ? QString::fromUtf8(f.readAll())
                                   : QString());
    m_fileState->setText(QString("Editing %1").arg(path));
}

QWidget *MainWindow::buildKeybindsPage() {
    auto *page = new QWidget;
    auto *v = new QVBoxLayout(page);

    auto *controls = new QHBoxLayout;
    m_bindCount = new QLabel;
    m_bindSearch = new QLineEdit;
    m_bindSearch->setPlaceholderText("Search shortcuts…   e.g. workspace, screenshot, terminal");
    m_bindSearch->setClearButtonEnabled(true);
    controls->addWidget(m_bindSearch, 1);
    controls->addWidget(m_bindCount);
    v->addLayout(controls);

    auto *actions = new QHBoxLayout;
    m_bindChange = new QPushButton("Change key…");
    m_bindDuplicate = new QPushButton("Duplicate…");
    m_bindReset = new QPushButton("Reset to stock");
    m_bindChange->setEnabled(false);
    m_bindDuplicate->setEnabled(false);
    m_bindReset->setEnabled(false);
    connect(m_bindChange, &QPushButton::clicked, this, &MainWindow::changeSelectedBind);
    connect(m_bindDuplicate, &QPushButton::clicked, this, &MainWindow::duplicateSelectedBind);
    connect(m_bindReset, &QPushButton::clicked, this, &MainWindow::resetSelectedBind);
    actions->addWidget(m_bindChange);
    actions->addWidget(m_bindDuplicate);
    actions->addWidget(m_bindReset);
    actions->addStretch(1);
    v->addLayout(actions);

    m_bindTable = new QTableWidget;
    m_bindTable->setColumnCount(2);
    m_bindTable->setHorizontalHeaderLabels({"Shortcut", "Action"});
    m_bindTable->verticalHeader()->setVisible(false);
    m_bindTable->verticalHeader()->setDefaultSectionSize(20);
    m_bindTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_bindTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_bindTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_bindTable->setShowGrid(false);
    m_bindTable->setAlternatingRowColors(true);
    m_bindTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_bindTable->horizontalHeader()->setStretchLastSection(true);
    m_bindTable->setColumnWidth(0, 220);
    v->addWidget(m_bindTable, 1);

    auto *note = new QLabel(
        "Some shortcuts are generated with Lua loops (workspaces 1–10, focus/move directions, "
        "keypad, media volume…) and are not listed individually. \"Change key\" and \"Duplicate\" "
        "are only offered for shortcuts whose action can be safely copied into the custom file. "
        "Rows painted orange clash with another shortcut; rows ending in a custom file are "
        "removable with \"Reset to stock\".");
    note->setStyleSheet("color: palette(mid); font-size: 11px;");
    note->setWordWrap(true);
    v->addWidget(note);

    connect(m_bindSearch, &QLineEdit::textChanged, this, &MainWindow::filterKeybinds);
    connect(m_bindTable, &QTableWidget::itemSelectionChanged, this, &MainWindow::updateBindButtons);
    populateKeybinds();
    return page;
}

void MainWindow::populateKeybinds() {
    const QVector<KeyBind> binds = m_model->keybinds();

    QHash<QString, int> chordCount;
    for (const KeyBind &b : binds) chordCount[b.chord]++;
    const QBrush clash(QColor(0xF2, 0xA9, 0x00, 0x40)); // orange -> duplicate chord

    m_bindTable->setRowCount(binds.size());
    for (int r = 0; r < binds.size(); ++r) {
        const KeyBind &b = binds.at(r);
        auto *key = new QTableWidgetItem(b.display);
        key->setTextAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        auto *act = new QTableWidgetItem(b.description.isEmpty() ? "—" : b.description);
        act->setTextAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        key->setData(BindDescRole, b.description);
        key->setData(BindStockRole, b.stockChord);
        key->setData(BindActionRole, b.action);
        key->setData(BindChordRole, b.chord);
        key->setData(BindEditableRole, b.editable);
        key->setData(BindCustomRole, b.fromCustom);
        key->setData(BindOptionsRole, b.options);
        const bool clashQ = chordCount.value(b.chord) > 1;
        QBrush bg = clashQ ? clash : QBrush();
        key->setBackground(bg);
        act->setBackground(bg);
        key->setToolTip(clashQ ? "This key is also bound to another shortcut." : QString());
        m_bindTable->setItem(r, 0, key);
        m_bindTable->setItem(r, 1, act);
    }
    m_bindTable->clearSelection();
    updateBindButtons();
}

const KeyBind *MainWindow::selectedBind() const {
    const int row = m_bindTable->currentRow();
    if (row < 0 || m_bindTable->isRowHidden(row)) return nullptr;
    QTableWidgetItem *it = m_bindTable->item(row, 0);
    if (!it) return nullptr;
    static thread_local KeyBind last;
    last.chord = it->data(BindChordRole).toString();
    last.display = formatChord(last.chord);
    last.description = it->data(BindDescRole).toString();
    last.stockChord = it->data(BindStockRole).toString();
    last.action = it->data(BindActionRole).toString();
    last.options = it->data(BindOptionsRole).toString();
    last.fromCustom = it->data(BindCustomRole).toBool();
    last.editable = it->data(BindEditableRole).toBool();
    last.rebound = last.fromCustom;
    return &last;
}

void MainWindow::refreshKeybinds() {
    populateKeybinds();
    filterKeybinds(m_bindSearch->text());
    refreshAppHints();
}

void MainWindow::refreshAppHints() {
    for (const AppField &f : appFields()) {
        if (auto *hint = m_appKeyHints.value(f.var)) {
            const QString kb = m_model->keybindForApp(f.var);
            hint->setText(kb.isEmpty() ? QString() : QString("Shortcut: %1").arg(kb));
        }
    }
}

void MainWindow::updateBindButtons() {
    const KeyBind *b = selectedBind();
    m_bindChange->setEnabled(b && b->editable);
    m_bindDuplicate->setEnabled(b && b->editable);
    m_bindReset->setEnabled(b && b->fromCustom);
}

void MainWindow::changeSelectedBind() {
    const KeyBind *b = selectedBind();
    if (!b || !b->editable) return;

    const QString subject = b->description.isEmpty() ? b->display : b->description;
    BindEditDialog dlg("Change shortcut: " + subject, b->chord, this);
    if (dlg.exec() != QDialog::Accepted) return;

    const QString newChord = dlg.chord();
    if (newChord.isEmpty() || newChord == b->chord) return;

    QString err;
    if (!m_model->remapBind(b->stockChord, b->action, newChord, &err)) {
        QMessageBox::warning(this, "Cannot change shortcut", err);
        return;
    }
    refreshKeybinds();
    m_status->setText(QString("Shortcut for \"%1\" is now %2. hyprctl reload requested.")
                          .arg(subject, formatChord(newChord)));
}

void MainWindow::duplicateSelectedBind() {
    const KeyBind *b = selectedBind();
    if (!b || !b->editable) return;

    const QString subject = b->description.isEmpty() ? b->display : b->description;
    BindEditDialog dlg("Duplicate shortcut as: " + subject, QString(), this);
    if (dlg.exec() != QDialog::Accepted) return;

    const QString newChord = dlg.chord();
    if (newChord.isEmpty()) return;

    QString err;
    if (!m_model->addBind(newChord, b->action, b->options, &err)) {
        QMessageBox::warning(this, "Cannot duplicate shortcut", err);
        return;
    }
    refreshKeybinds();
    m_status->setText(QString("Added %1 as \"%2\". hyprctl reload requested.")
                          .arg(formatChord(newChord), subject));
}

void MainWindow::resetSelectedBind() {
    const KeyBind *b = selectedBind();
    if (!b || !b->fromCustom) return;

    const auto ret = QMessageBox::question(
        this, "Reset shortcut",
        QString("Remove the custom key for \"%1\" (%2)?\n"
                "This restores the stock binding.")
            .arg(b->description.isEmpty() ? "this shortcut" : b->description, b->display),
        QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    QString err;
    const QString markerChord = b->stockChord.isEmpty() ? b->chord : b->stockChord;
    if (!m_model->resetBind(markerChord, b->action, &err)) {
        QMessageBox::warning(this, "Cannot reset shortcut", err);
        return;
    }
    refreshKeybinds();
    m_status->setText("Restored the stock shortcut. hyprctl reload requested.");
}

void MainWindow::filterKeybinds(const QString &query) {
    int shown = 0;
    for (int r = 0; r < m_bindTable->rowCount(); ++r) {
        const QString key = m_bindTable->item(r, 0) ? m_bindTable->item(r, 0)->text() : QString();
        const QString act = m_bindTable->item(r, 1) ? m_bindTable->item(r, 1)->text() : QString();
        const QString combined = key + " " + act;
        const bool visible = query.isEmpty() || combined.contains(query, Qt::CaseInsensitive);
        m_bindTable->setRowHidden(r, !visible);
        if (visible) ++shown;
    }
    m_bindCount->setText(QString("%1 / %2").arg(shown).arg(m_bindTable->rowCount()));
}

// Convert a value read from the config tree into the widget.
static void setWidgetValue(const Bound &b, const QVariant &v) {
    const SettingSpec &s = b.spec;
    switch (s.type) {
    case SettingSpec::Bool: {
        auto *cb = qobject_cast<QCheckBox *>(b.widget);
        if (cb) cb->setChecked(v.isValid() && v.toBool());
        break;
    }
    case SettingSpec::Int: {
        auto *spin = qobject_cast<QSpinBox *>(b.widget);
        if (spin) spin->setValue(v.isValid() ? v.toInt() : spin->minimum());
        break;
    }
    case SettingSpec::Double: {
        auto *spin = qobject_cast<QDoubleSpinBox *>(b.widget);
        if (spin) spin->setValue(v.isValid() ? v.toDouble() : spin->minimum());
        break;
    }
    case SettingSpec::String: {
        auto *le = qobject_cast<QLineEdit *>(b.widget);
        if (le) le->setText(v.isValid() ? v.toString() : QString());
        break;
    }
    case SettingSpec::Color: {
        auto *btn = qobject_cast<ColorButton *>(b.widget);
        // The tree stores colors as rgba(rrggbbaa) config strings. Only treat a
        // value as a QColor when it really is one; QVariant's automatic
        // QString -> QColor cast would reject the rgba(...) syntax and produce
        // an invalid (black) color.
        QColor c;
        if (v.metaType() == QMetaType::fromType<QColor>())
            c = v.value<QColor>();
        else
            c = hyprColorFromConfig(v.toString());
        if (btn) btn->setColor(c);
        break;
    }
    case SettingSpec::Combo: {
        auto *combo = qobject_cast<QComboBox *>(b.widget);
        if (!combo) break;
        double want = v.isValid() ? v.toDouble() : combo->itemData(0).toDouble();
        int idx = -1;
        for (int i = 0; i < combo->count(); ++i) {
            if (qFuzzyCompare(combo->itemData(i).toDouble(), want)) { idx = i; break; }
        }
        combo->setCurrentIndex(idx < 0 ? 0 : idx);
        break;
    }
    }
}

// Convert the current widget state into the config value.
static QVariant widgetValue(const Bound &b) {
    const SettingSpec &s = b.spec;
    switch (s.type) {
    case SettingSpec::Bool:
        if (auto *cb = qobject_cast<QCheckBox *>(b.widget)) return QVariant(cb->isChecked());
        break;
    case SettingSpec::Int:
        if (auto *spin = qobject_cast<QSpinBox *>(b.widget)) return QVariant(spin->value());
        break;
    case SettingSpec::Double:
        if (auto *spin = qobject_cast<QDoubleSpinBox *>(b.widget)) return QVariant(spin->value());
        break;
    case SettingSpec::String:
        if (auto *le = qobject_cast<QLineEdit *>(b.widget)) return QVariant(le->text());
        break;
    case SettingSpec::Color:
        if (auto *btn = qobject_cast<ColorButton *>(b.widget)) return QVariant::fromValue(btn->color());
        break;
    case SettingSpec::Combo:
        if (auto *combo = qobject_cast<QComboBox *>(b.widget)) return combo->currentData();
        break;
    }
    return QVariant();
}

// Human-readable text of a config value for the diff popup.
static QString configValueText(const QVariant &v, SettingSpec::Type type) {
    if (!v.isValid()) return "(default)";
    switch (type) {
    case SettingSpec::Bool:   return v.toBool() ? "on" : "off";
    case SettingSpec::Double: return QString::number(v.toDouble(), 'g', 3);
    case SettingSpec::Color: {
        if (v.metaType() == QMetaType::fromType<QColor>())
            return hyprColorToConfig(v.value<QColor>());
        if (v.canConvert<QString>()) return v.toString();
        return "(default)";
    }
    case SettingSpec::Combo:   return QString::number(v.toDouble());
    case SettingSpec::String:  return v.toString();
    case SettingSpec::Int:     return QString::number(v.toInt());
    }
    return v.toString();
}

void MainWindow::snapshotInitialValues() {
    m_initialWidgetValues.clear();
    for (const Bound &b : m_generalBindings)
        m_initialWidgetValues.insert(b.spec.key, widgetValue(b));
    m_initialAppValues.clear();
    for (const AppField &f : appFields())
        if (auto *le = m_appFields.value(f.var))
            m_initialAppValues.insert(f.var, le->text().trimmed());
    m_initialLockMin = m_lockMin->value();
    m_initialDpmsMin = m_dpmsMin->value();
    m_initialSuspendMin = m_suspendMin->value();
}

void MainWindow::loadValues() {
    for (const Bound &b : m_generalBindings)
        setWidgetValue(b, m_model->valueAt(b.spec.key));

    m_lockMin->setValue(m_model->idleLockSec / 60);
    m_dpmsMin->setValue(m_model->idleDpmsSec / 60);
    m_suspendMin->setValue(m_model->idleSuspendSec / 60);

    for (const AppField &f : appFields()) {
        QVariant v = m_model->overrideVar(f.var);
        if (!v.isValid() || v.toString().isEmpty())
            v = m_model->defaultVar(f.var);
        if (auto *le = m_appFields.value(f.var))
            le->setText(v.toString());
    }
    refreshAppHints();
    snapshotInitialValues();
}

void MainWindow::saveValues() {
    QList<GeneralEdit> edits;
    QList<QString> summary; // one "key (before -> after)" line per real change
    for (const Bound &b : m_generalBindings) {
        GeneralEdit e;
        e.type = b.spec.type;
        e.key = b.spec.key;
        e.value = widgetValue(b);
        edits.append(e);

        const QVariant before = m_initialWidgetValues.contains(b.spec.key)
                                    ? m_initialWidgetValues.value(b.spec.key)
                                    : m_model->valueAt(b.spec.key);
        if (before == e.value) continue; // untouched this session
        summary << QString("%1\n    %2  ->  %3")
                       .arg(b.spec.key, configValueText(before, b.spec.type),
                            configValueText(e.value, b.spec.type));
    }

    QMap<QString, QString> varChanges;
    for (const AppField &f : appFields()) {
        if (auto *le = m_appFields.value(f.var)) {
            const QString newVal = le->text().trimmed();
            varChanges.insert(f.var, newVal);
            if (m_initialAppValues.value(f.var) != newVal)
                summary << QString("%1  %2 -> %3")
                               .arg(f.var, m_initialAppValues.value(f.var), newVal);
        }
    }

    const int lockSec = m_lockMin->value() * 60;
    const int dpmsSec = m_dpmsMin->value() * 60;
    const int suspendSec = m_suspendMin->value() * 60;
    if (m_initialLockMin != m_lockMin->value())
        summary << QString("lock screen   %1 min -> %2 min").arg(m_initialLockMin).arg(m_lockMin->value());
    if (m_initialDpmsMin != m_dpmsMin->value())
        summary << QString("display off   %1 min -> %2 min").arg(m_initialDpmsMin).arg(m_dpmsMin->value());
    if (m_initialSuspendMin != m_suspendMin->value())
        summary << QString("suspend       %1 min -> %2 min").arg(m_initialSuspendMin).arg(m_suspendMin->value());

    if (m_model->save(edits, varChanges, lockSec, dpmsSec, suspendSec)) {
        loadValues();
        m_status->setText(QString("Saved %1 change(s). File written under %2 and reload requested.")
                              .arg(summary.size()).arg(m_model->configPath()));

        if (!summary.isEmpty())
            showChangesDialog(summary.join('\n'));
    } else {
        m_status->setText("Failed to write config files. Check permissions.");
    }
}

#include "mainwindow.moc"