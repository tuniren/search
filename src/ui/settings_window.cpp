#include "settings_window.h"

#include "theme.h"
#include "version.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace searchapp
{
namespace
{

QLabel* muted(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("muted"));
    label->setWordWrap(true);
    return label;
}

QVBoxLayout* pageLayout(QWidget* page, const QString& title, const QString& description)
{
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(28, 22, 28, 20);
    layout->setSpacing(18);
    auto* heading = new QLabel(title, page);
    heading->setObjectName(QStringLiteral("pageTitle"));
    layout->addWidget(heading);
    layout->addWidget(muted(description, page));
    return layout;
}

} // namespace

SettingsWindow::SettingsWindow(QWidget* parent)
    : QWidget(parent, Qt::Window), shortcut_(new QKeySequenceEdit(this)),
      launchAtLogin_(new QCheckBox(QStringLiteral("登录 Windows 时启动 search"), this)),
      hideOnFocusLoss_(new QCheckBox(QStringLiteral("点击其他窗口时收起搜索面板"), this)),
      checkForUpdates_(new QCheckBox(QStringLiteral("自动检查新版本（每天最多一次）"), this)),
      theme_(new QComboBox(this)), status_(new QLabel(this)), error_(new QLabel(this)),
      updateMessage_(new QLabel(this)), releaseNotes_(new QPlainTextEdit(this)),
      updateProgress_(new QProgressBar(this)),
      checkUpdate_(new QPushButton(QStringLiteral("检查更新"), this)), updateAction_(new QPushButton(this)),
      cancelUpdate_(new QPushButton(QStringLiteral("取消下载"), this))
{
    setObjectName(QStringLiteral("settingsRoot"));
    setWindowTitle(QStringLiteral("search 设置"));
    setWindowIcon(glyphIcon(Glyph::Search));
    setMinimumSize(760, 600);
    resize(880, 680);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    auto* body = new QHBoxLayout;
    body->setContentsMargins(16, 20, 0, 0);
    body->setSpacing(0);
    auto* sidebar = new QWidget(this);
    sidebar->setFixedWidth(174);
    auto* sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(0, 0, 14, 0);
    auto* brand = new QLabel(QStringLiteral("search"), sidebar);
    brand->setObjectName(QStringLiteral("sectionTitle"));
    sideLayout->addWidget(brand);
    sideLayout->addWidget(muted(QStringLiteral("随唤随到"), sidebar));
    sideLayout->addSpacing(20);
    auto* navigation = new QListWidget(sidebar);
    navigation->setObjectName(QStringLiteral("navigation"));
    navigation->setAccessibleName(QStringLiteral("设置分类"));
    navigation->addItems(
        {QStringLiteral("常规"), QStringLiteral("外观"), QStringLiteral("程序搜索"), QStringLiteral("关于")});
    sideLayout->addWidget(navigation, 1);
    sideLayout->addWidget(muted(QStringLiteral("版本 %1").arg(QString::fromLatin1(appVersion)), sidebar));
    sideLayout->addSpacing(16);
    body->addWidget(sidebar);
    auto* pages = new QStackedWidget(this);
    body->addWidget(pages, 1);
    outer->addLayout(body, 1);

    auto* general = new QWidget(pages);
    auto* generalLayout =
        pageLayout(general, QStringLiteral("常规"), QStringLiteral("让 search 配合你的工作习惯。"));
    auto* hotkeyGroup = new QGroupBox(QStringLiteral("唤起快捷键"), general);
    auto* hotkeyLayout = new QVBoxLayout(hotkeyGroup);
    shortcut_->setMaximumSequenceLength(1);
    shortcut_->setAccessibleName(QStringLiteral("全局唤起快捷键"));
    hotkeyLayout->addWidget(shortcut_);
    hotkeyLayout->addWidget(muted(
        QStringLiteral("点击后按下快捷键。若被其他程序占用，保存时会提示；原快捷键继续有效。"), hotkeyGroup));
    generalLayout->addWidget(hotkeyGroup);
    auto* behavior = new QGroupBox(QStringLiteral("运行行为"), general);
    auto* behaviorLayout = new QVBoxLayout(behavior);
    behaviorLayout->setSpacing(18);
    behaviorLayout->addWidget(launchAtLogin_);
    behaviorLayout->addWidget(hideOnFocusLoss_);
    behaviorLayout->addWidget(checkForUpdates_);
    behaviorLayout->addWidget(
        muted(QStringLiteral("更新检查会连接 GitHub，仅检查版本；下载和重启由你决定。"), behavior));
    behaviorLayout->addWidget(
        muted(QStringLiteral("关闭搜索或设置窗口后仍在托盘运行，可从托盘菜单退出。"), behavior));
    generalLayout->addWidget(behavior);
    generalLayout->addStretch();
    pages->addWidget(general);

    auto* appearance = new QWidget(pages);
    auto* appearanceLayout =
        pageLayout(appearance, QStringLiteral("外观"), QStringLiteral("清晰、轻盈，专注于要找的内容。"));
    auto* appearanceGroup = new QGroupBox(QStringLiteral("主题"), appearance);
    auto* appearanceForm = new QFormLayout(appearanceGroup);
    theme_->addItems({QStringLiteral("跟随系统"), QStringLiteral("浅色"), QStringLiteral("深色")});
    theme_->setObjectName(QStringLiteral("themeSelector"));
    appearanceForm->addRow(QStringLiteral("配色模式"), theme_);
    appearanceLayout->addWidget(appearanceGroup);
    appearanceLayout->addWidget(
        muted(QStringLiteral("主题在保存后立即应用。搜索面板和设置窗口使用同一套配色。"), appearance));
    appearanceLayout->addStretch();
    pages->addWidget(appearance);

    auto* searchPage = new QWidget;
    auto* searchLayout =
        pageLayout(searchPage, QStringLiteral("程序搜索"), QStringLiteral("搜索 Windows 开始菜单中的程序。"));
    auto* sources = new QGroupBox(QStringLiteral("搜索范围"), searchPage);
    auto* sourcesLayout = new QVBoxLayout(sources);
    sourcesLayout->addWidget(muted(
        QStringLiteral("自动收录当前用户和所有用户的开始菜单程序，以及 Windows 注册的商店应用。"), sources));
    sourcesLayout->addWidget(muted(
        QStringLiteral(
            "支持程序原名、中文全拼和拼音首字母。Windows 系统程序同时支持中英文名称；第三方程序保留原名。"),
        sources));
    searchLayout->addWidget(sources);
    status_->setObjectName(QStringLiteral("muted"));
    status_->setWordWrap(true);
    searchLayout->addWidget(status_);
    auto* maintenance = new QHBoxLayout;
    auto* refresh = new QPushButton(QStringLiteral("刷新程序列表"), searchPage);
    auto* clearHistory = new QPushButton(QStringLiteral("清除使用历史"), searchPage);
    maintenance->addWidget(refresh);
    maintenance->addWidget(clearHistory);
    maintenance->addStretch();
    searchLayout->addLayout(maintenance);
    searchLayout->addStretch();
    auto* searchScroll = new QScrollArea(pages);
    searchScroll->setWidgetResizable(true);
    searchScroll->setFrameShape(QFrame::NoFrame);
    searchScroll->setWidget(searchPage);
    pages->addWidget(searchScroll);

    auto* about = new QWidget(pages);
    auto* aboutLayout = pageLayout(about, QStringLiteral("关于 search"), QStringLiteral("找到，然后继续。"));
    auto* logo = new QLabel(about);
    logo->setPixmap(glyphIcon(Glyph::Search).pixmap(72, 72));
    aboutLayout->addSpacing(24);
    aboutLayout->addWidget(logo);
    aboutLayout->addWidget(
        new QLabel(QStringLiteral("search %1").arg(QString::fromLatin1(appVersion)), about));
    auto* repository = new QLabel(
        QStringLiteral("<a href=\"https://github.com/tuniren/search\">https://github.com/tuniren/search</a>"),
        about);
    repository->setOpenExternalLinks(true);
    repository->setTextInteractionFlags(Qt::TextBrowserInteraction);
    repository->setAccessibleName(QStringLiteral("GitHub 仓库"));
    aboutLayout->addWidget(repository);
    auto* updates = new QGroupBox(QStringLiteral("软件更新"), about);
    auto* updateLayout = new QVBoxLayout(updates);
    updateMessage_->setWordWrap(true);
    updateMessage_->setTextFormat(Qt::PlainText);
    updateMessage_->setObjectName(QStringLiteral("updateMessage"));
    updateLayout->addWidget(updateMessage_);
    releaseNotes_->setReadOnly(true);
    releaseNotes_->setMaximumHeight(110);
    releaseNotes_->setAccessibleName(QStringLiteral("新版本说明"));
    updateLayout->addWidget(releaseNotes_);
    updateProgress_->setAccessibleName(QStringLiteral("更新下载进度"));
    updateLayout->addWidget(updateProgress_);
    auto* updateButtons = new QHBoxLayout;
    checkUpdate_->setObjectName(QStringLiteral("checkUpdate"));
    updateAction_->setObjectName(QStringLiteral("updateAction"));
    cancelUpdate_->setObjectName(QStringLiteral("cancelUpdate"));
    updateButtons->addWidget(checkUpdate_);
    updateButtons->addWidget(updateAction_);
    updateButtons->addWidget(cancelUpdate_);
    updateButtons->addStretch();
    updateLayout->addLayout(updateButtons);
    auto* releases = new QLabel(
        QStringLiteral("<a href=\"https://github.com/tuniren/search/releases\">打开 GitHub 发布页</a>"),
        updates);
    releases->setOpenExternalLinks(true);
    updateLayout->addWidget(releases);
    aboutLayout->addWidget(updates);
    aboutLayout->addWidget(muted(
        QStringLiteral("C++20 · Qt 6 Widgets\n命令式 C++ 界面，Windows 系统集成。\n\n使用 ↑ ↓ 选择，Enter "
                       "打开，Shift+Enter 打开所在位置。\n右键结果可以固定常用项或设置搜索别名。"),
        about));
    aboutLayout->addWidget(
        muted(QStringLiteral("索引、设置及历史保存在本机用户目录，输入内容不会自动上传。"), about));
    aboutLayout->addStretch();
    auto* aboutScroll = new QScrollArea(pages);
    aboutScroll->setWidgetResizable(true);
    aboutScroll->setFrameShape(QFrame::NoFrame);
    aboutScroll->setWidget(about);
    pages->addWidget(aboutScroll);
    setUpdateStatus({});

    auto* footer = new QHBoxLayout;
    footer->setContentsMargins(22, 14, 22, 16);
    error_->setWordWrap(true);
    error_->setStyleSheet(QStringLiteral("color: #db645b;"));
    footer->addWidget(error_, 1);
    auto* cancel = new QPushButton(QStringLiteral("取消"), this);
    auto* save = new QPushButton(QStringLiteral("保存设置"), this);
    save->setObjectName(QStringLiteral("primary"));
    save->setAccessibleName(QStringLiteral("保存设置"));
    footer->addWidget(cancel);
    footer->addWidget(save);
    outer->addLayout(footer);

    connect(navigation, &QListWidget::currentRowChanged, pages, &QStackedWidget::setCurrentIndex);
    navigation->setCurrentRow(0);
    connect(cancel, &QPushButton::clicked, this, [this] {
        setSettings(original_);
        hide();
    });
    connect(save, &QPushButton::clicked, this, &SettingsWindow::collectAndSave);
    connect(refresh, &QPushButton::clicked, this, &SettingsWindow::refreshRequested);
    connect(checkUpdate_, &QPushButton::clicked, this, &SettingsWindow::checkUpdateRequested);
    connect(cancelUpdate_, &QPushButton::clicked, this, &SettingsWindow::cancelUpdateRequested);
    connect(updateAction_, &QPushButton::clicked, this, [this] {
        if (updateState_ == UpdateState::Available)
            emit downloadUpdateRequested();
        else if (updateState_ == UpdateState::Ready &&
                 QMessageBox::question(
                     this, QStringLiteral("重启更新"),
                     QStringLiteral("search 将退出、安装已下载的更新并重新启动。现在继续？")) ==
                     QMessageBox::Yes)
            emit installUpdateRequested();
    });
    connect(clearHistory, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, QStringLiteral("清除使用历史"),
                                  QStringLiteral("清除打开次数及最近使用记录？固定项和别名会保留。")) ==
            QMessageBox::Yes)
            emit clearHistoryRequested();
    });
}

void SettingsWindow::setSettings(const Settings& value)
{
    original_ = value;
    shortcut_->setKeySequence(QKeySequence::fromString(value.hotkey, QKeySequence::PortableText));
    launchAtLogin_->setChecked(value.launchAtLogin);
    hideOnFocusLoss_->setChecked(value.hideOnFocusLoss);
    checkForUpdates_->setChecked(value.checkForUpdates);
    theme_->setCurrentIndex(static_cast<int>(value.theme));
    error_->clear();
}

void SettingsWindow::setStatus(const QString& status)
{
    status_->setText(status);
}
void SettingsWindow::setError(const QString& error)
{
    error_->setStyleSheet(QStringLiteral("color: #db645b;"));
    error_->setText(error);
}

void SettingsWindow::showSaved()
{
    error_->setStyleSheet(QStringLiteral("color: #438563;"));
    error_->setText(QStringLiteral("设置已保存"));
}

void SettingsWindow::setUpdateStatus(const UpdateStatus& status)
{
    updateState_ = status.state;
    updateMessage_->setText(status.message);
    releaseNotes_->setPlainText(status.notes);
    releaseNotes_->setVisible(!status.notes.isEmpty());
    const bool downloading = status.state == UpdateState::Downloading;
    const bool checking = status.state == UpdateState::Checking;
    checkUpdate_->setEnabled(!checking && !downloading && status.state != UpdateState::Ready);
    updateAction_->setVisible(status.state == UpdateState::Available || status.state == UpdateState::Ready);
    updateAction_->setText(status.state == UpdateState::Ready ? QStringLiteral("重启更新")
                                                              : QStringLiteral("下载更新"));
    cancelUpdate_->setVisible(downloading);
    updateProgress_->setVisible(downloading || checking);
    updateProgress_->setRange(0, checking ? 0 : 1000);
    updateProgress_->setValue(status.total > 0 ? static_cast<int>(status.received * 1000 / status.total) : 0);
    updateProgress_->setFormat(QStringLiteral("%1 / %2 MB")
                                   .arg(status.received / 1e6, 0, 'f', 2)
                                   .arg(status.total / 1e6, 0, 'f', 2));
}

void SettingsWindow::showUpdates()
{
    findChild<QListWidget*>(QStringLiteral("navigation"))->setCurrentRow(3);
}

void SettingsWindow::collectAndSave()
{
    Settings value = original_;
    value.hotkey = shortcut_->keySequence().toString(QKeySequence::PortableText);
    value.launchAtLogin = launchAtLogin_->isChecked();
    value.hideOnFocusLoss = hideOnFocusLoss_->isChecked();
    value.checkForUpdates = checkForUpdates_->isChecked();
    value.theme = static_cast<Theme>(theme_->currentIndex());
    emit saveRequested(value);
}

} // namespace searchapp
