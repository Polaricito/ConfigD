#pragma once

#include <QHash>
#include <QMainWindow>
#include <QVector>
#include <QVariant>

#include "settings.h"

class QListWidget;
class QStackedWidget;
class QLabel;
class QSpinBox;
class QLineEdit;
class QPushButton;
class QWidget;
class QTableWidget;
class QComboBox;
class QPlainTextEdit;

struct Bound {
    SettingSpec spec;
    QWidget *widget = nullptr;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    void buildUi();
    QWidget *buildSpecPage(const QString &section);
    QWidget *buildIdlePage();
    QWidget *buildAppsPage();
    QWidget *buildKeybindsPage();
    QWidget *buildCustomFilesPage();

    void loadValues();
    void saveValues();
    void snapshotInitialValues();
    void filterKeybinds(const QString &query);
    void populateKeybinds();
    void refreshKeybinds();
    void refreshAppHints();
    void updateBindButtons();
    void changeSelectedBind();
    void resetSelectedBind();
    void duplicateSelectedBind();
    void filterSpecRows(const QString &query);
    void refreshCustomEditors();
    void applyStyle(const QString &name);
    void applyIconTheme(int index);
    void installDesktopIntegration();
    void showChangesDialog(const QString &summary);

    const KeyBind *selectedBind() const;

    SettingsModel *m_model = nullptr;

    QListWidget *m_nav = nullptr;
    QStackedWidget *m_stack = nullptr;
    QLabel *m_status = nullptr;
    QLineEdit *m_globalSearch = nullptr;
    QComboBox *m_themeCombo = nullptr;
    QComboBox *m_iconThemeCombo = nullptr;
    QString m_systemStyleName;
    QString m_systemIconTheme;
    QDialog *m_changesDialog = nullptr;
    QPlainTextEdit *m_changesTxt = nullptr;

    QVector<Bound> m_generalBindings;
    QHash<QString, QPair<QLabel *, QWidget *>> m_specRowWidgets; // spec.key -> label + field widget
    QHash<QString, QLineEdit *> m_appFields;
    QHash<QString, QLabel *> m_appKeyHints;
    QSpinBox *m_lockMin = nullptr;
    QSpinBox *m_dpmsMin = nullptr;
    QSpinBox *m_suspendMin = nullptr;

    // Widget state as it was when last (re)loaded, used to detect real changes.
    QHash<QString, QVariant> m_initialWidgetValues;
    QHash<QString, QString> m_initialAppValues;
    int m_initialLockMin = 0, m_initialDpmsMin = 0, m_initialSuspendMin = 0;

    QLineEdit *m_bindSearch = nullptr;
    QTableWidget *m_bindTable = nullptr;
    QLabel *m_bindCount = nullptr;
    QPushButton *m_bindChange = nullptr;
    QPushButton *m_bindReset = nullptr;
    QPushButton *m_bindDuplicate = nullptr;

    QComboBox *m_filePicker = nullptr;
    QPlainTextEdit *m_fileEditor = nullptr;
    QLabel *m_fileState = nullptr;
};