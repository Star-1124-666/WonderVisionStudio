#include "MainWindow.h"

#include <chrono>

#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMovie>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSaveFile>
#include <QSlider>
#include <QSizePolicy>
#include <QSplitter>
#include <QStringList>
#include <QStatusBar>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QXmlStreamReader>

namespace
{
    struct MainWindowSearch
    {
        DWORD process_id;
        HWND window;
    };

    BOOL CALLBACK FindProcessMainWindow(HWND window, LPARAM parameter)
    {
        auto* search = reinterpret_cast<MainWindowSearch*>(parameter);
        DWORD process_id = 0;
        GetWindowThreadProcessId(window, &process_id);
        if(process_id == search->process_id && IsWindowVisible(window) && GetWindow(window, GW_OWNER) == nullptr)
        {
            search->window = window;
            return FALSE;
        }
        return TRUE;
    }

    bool SetLhmStartupSetting(QString& xml, const QString& key, const QString& value)
    {
        const QString escaped_key = QRegularExpression::escape(key);
        const QRegularExpression setting_tag(QStringLiteral("<add\\b(?=[^>]*\\bkey\\s*=\\s*['\\\"]%1['\\\"])[^>]*?/?\\s*>")
            .arg(escaped_key), QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch match = setting_tag.match(xml);
        if(match.hasMatch())
        {
            QString tag = match.captured();
            const QRegularExpression value_attribute(QStringLiteral("\\s+value\\s*=\\s*(\"[^\"]*\"|'[^']*')"), QRegularExpression::CaseInsensitiveOption);
            if(value_attribute.match(tag).hasMatch())
            {
                tag.replace(value_attribute, QStringLiteral(" value=\"%1\"").arg(value));
            }
            else
            {
                const int insert_at = tag.lastIndexOf(QStringLiteral("/>")) >= 0
                    ? tag.lastIndexOf(QStringLiteral("/>")) : tag.lastIndexOf(QLatin1Char('>'));
                if(insert_at < 0) return false;
                tag.insert(insert_at, QStringLiteral(" value=\"%1\"").arg(value));
            }
            xml.replace(match.capturedStart(), match.capturedLength(), tag);
            return true;
        }

        const QString new_setting = QStringLiteral("<add key=\"%1\" value=\"%2\" />").arg(key, value);
        const int settings_end = xml.indexOf(QStringLiteral("</appSettings>"), 0, Qt::CaseInsensitive);
        if(settings_end >= 0)
        {
            xml.insert(settings_end, new_setting);
            return true;
        }

        const int config_end = xml.indexOf(QStringLiteral("</configuration>"), 0, Qt::CaseInsensitive);
        if(config_end < 0) return false;
        xml.insert(config_end, QStringLiteral("<appSettings>%1</appSettings>").arg(new_setting));
        return true;
    }

    bool PrepareLhmForTrayAndSensors(const QString& executable, QString& error)
    {
        const QString config_path = QDir(QFileInfo(executable).absolutePath()).filePath(QStringLiteral("LibreHardwareMonitor.config"));
        QString xml = QStringLiteral("<?xml version=\"1.0\" encoding=\"utf-8\"?><configuration><appSettings></appSettings></configuration>");
        QFile existing_config(config_path);
        if(existing_config.exists())
        {
            if(!existing_config.open(QIODevice::ReadOnly))
            {
                error = QStringLiteral("Could not read Libre Hardware Monitor's settings file.");
                return false;
            }
            xml = QString::fromUtf8(existing_config.readAll());
        }

        if(!SetLhmStartupSetting(xml, QStringLiteral("startMinMenuItem"), QStringLiteral("true"))
            || !SetLhmStartupSetting(xml, QStringLiteral("minTrayMenuItem"), QStringLiteral("true"))
            || !SetLhmStartupSetting(xml, QStringLiteral("runWebServerMenuItem"), QStringLiteral("true")))
        {
            error = QStringLiteral("Libre Hardware Monitor's settings file has an unexpected format; it was left unchanged.");
            return false;
        }

        QXmlStreamReader validator(xml);
        while(!validator.atEnd()) validator.readNext();
        if(validator.hasError())
        {
            error = QStringLiteral("Libre Hardware Monitor's settings file is not valid XML; it was left unchanged.");
            return false;
        }

        QSaveFile output(config_path);
        const QByteArray bytes = xml.toUtf8();
        if(!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit())
        {
            error = QStringLiteral("Could not save Libre Hardware Monitor's tray settings. Check that its folder is writable.");
            return false;
        }
        return true;
    }

    QString Metric(int value, bool valid, const QString& suffix)
    {
        return valid ? QString::number(value) + suffix : QStringLiteral("--") + suffix;
    }
}

MainWindow::MainWindow(bool start_screen_on_launch, bool start_minimized_to_tray, QWidget* parent)
    : QMainWindow(parent), preview(nullptr), preview_image(nullptr), media_name(nullptr),
      sensor_status(nullptr), device_status(nullptr), start_button(nullptr), stop_button(nullptr),
      save_config_button(nullptr), load_config_button(nullptr), start_with_windows_checkbox(nullptr),
      cpu_temp_combo(nullptr), cpu_load_combo(nullptr), gpu_temp_combo(nullptr), gpu_load_combo(nullptr),
      fit_combo(nullptr), show_cpu_temp(nullptr), show_cpu_load(nullptr),
      show_gpu_temp(nullptr), show_gpu_load(nullptr), show_clock(nullptr), bold_text(nullptr), italic_text(nullptr),
      outline_text(nullptr),
      font_size_slider(nullptr), text_x_slider(nullptr), text_y_slider(nullptr), background_opacity_slider(nullptr),
      outline_width_slider(nullptr), corner_radius_slider(nullptr), text_padding_slider(nullptr),
      font_family_combo(nullptr), text_color_button(nullptr), background_color_button(nullptr), outline_color_button(nullptr),
      custom_text_edit(nullptr),
      text_alignment_combo(nullptr), overlay_text_color(Qt::white), overlay_background_color(0, 0, 0),
      overlay_outline_color(Qt::black),
      ui_timer(nullptr), movie(nullptr), media_scale_dirty(true), output_dirty(true), animated(false), sensor_stop(false),
      refresh_sensors_requested(true), sensor_catalog_updated(false), sensor_catalog_ok(false),
      browse_lhm_button(nullptr), launch_lhm_with_app_checkbox(nullptr), lhm_path_edit(nullptr), tray_icon(nullptr),
      startup_retry_timer(nullptr), startup_retry_count(0), startup_screen_pending(false), quit_requested(false)
{
    setWindowTitle(QStringLiteral("Wonder Vision Studio"));
    resize(1120, 760);
    BuildUi();

    ui_timer = new QTimer(this);
    ui_timer->setInterval(50);
    connect(ui_timer, &QTimer::timeout, this, &MainWindow::Tick);
    ui_timer->start();

    startup_retry_timer = new QTimer(this);
    startup_retry_timer->setInterval(5000);
    connect(startup_retry_timer, &QTimer::timeout, this, &MainWindow::RetryStartupScreen);

    LoadLastConfiguration();
    RefreshStartupRegistration();
    if(launch_lhm_with_app_checkbox->isChecked() && QFileInfo(lhm_path_edit->text()).isFile())
    {
        if(!UpdateLhmStartupRegistration(true))
            SetUiStatus(QStringLiteral("Could not enable Libre Hardware Monitor startup."));
    }
    sensor_thread = std::thread(&MainWindow::SensorLoop, this);
    statusBar()->showMessage(QStringLiteral("Open an image or GIF to begin."));
    if(launch_lhm_with_app_checkbox->isChecked())
    {
        QTimer::singleShot(1200, this, &MainWindow::LaunchLhm);
    }
    if(start_screen_on_launch)
    {
        QTimer::singleShot(1800, this, [this]()
        {
            if(!media_frame.isNull())
            {
                startup_screen_pending = true;
                startup_retry_count = 0;
                StartScreen();
                startup_retry_timer->start();
            }
            else SetUiStatus(QStringLiteral("Save a config with an image before enabling screen startup."));
        });
    }
    if(start_minimized_to_tray && !QSystemTrayIcon::isSystemTrayAvailable())
    {
        SetUiStatus(QStringLiteral("Waiting for the Windows system tray icon to become available."));
    }
}

MainWindow::~MainWindow()
{
    device.Stop();
    sensor_stop = true;
    if(sensor_thread.joinable())
    {
        sensor_thread.join();
    }
}

void MainWindow::BuildUi()
{
    QWidget* central = new QWidget(this);
    QHBoxLayout* root = new QHBoxLayout(central);
    QSplitter* splitter = new QSplitter(Qt::Horizontal, central);

    preview = new QWidget(splitter);
    QVBoxLayout* preview_layout = new QVBoxLayout(preview);
    QHBoxLayout* file_row = new QHBoxLayout();
    QPushButton* open_button = new QPushButton(QStringLiteral("Open Image / GIF..."), preview);
    media_name = new QLabel(QStringLiteral("No file selected"), preview);
    media_name->setWordWrap(true);
    file_row->addWidget(open_button);
    file_row->addWidget(media_name, 1);
    preview_layout->addLayout(file_row);

    preview_image = new QLabel(preview);
    preview_image->setMinimumSize(540, 300);
    preview_image->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    preview_image->setAlignment(Qt::AlignCenter);
    preview_image->setStyleSheet(QStringLiteral("QLabel { background: #090d14; border: 1px solid #344152; color: #aab7c8; }"));
    preview_image->setText(QStringLiteral("Your screen preview will appear here"));
    preview_layout->addWidget(preview_image, 1);

    QScrollArea* controls_scroll = new QScrollArea(splitter);
    controls_scroll->setWidgetResizable(true);
    QWidget* controls = new QWidget(controls_scroll);
    controls->setMinimumWidth(325);
    QVBoxLayout* controls_layout = new QVBoxLayout(controls);

    QGroupBox* device_group = new QGroupBox(QStringLiteral("Wonder Vision"), controls);
    QVBoxLayout* device_layout = new QVBoxLayout(device_group);
    device_status = new QLabel(device.Status(), device_group);
    device_status->setWordWrap(true);
    device_layout->addWidget(device_status);
    QHBoxLayout* device_buttons = new QHBoxLayout();
    start_button = new QPushButton(QStringLiteral("Start Screen"), device_group);
    stop_button = new QPushButton(QStringLiteral("Stop"), device_group);
    stop_button->setEnabled(false);
    device_buttons->addWidget(start_button);
    device_buttons->addWidget(stop_button);
    device_layout->addLayout(device_buttons);
    QHBoxLayout* config_buttons = new QHBoxLayout();
    save_config_button = new QPushButton(QStringLiteral("Save Config..."), device_group);
    load_config_button = new QPushButton(QStringLiteral("Load Config..."), device_group);
    config_buttons->addWidget(save_config_button);
    config_buttons->addWidget(load_config_button);
    device_layout->addLayout(config_buttons);
    controls_layout->addWidget(device_group);

    QTabWidget* tabs = new QTabWidget(controls);

    QWidget* display_page = new QWidget(tabs);
    QVBoxLayout* display_layout = new QVBoxLayout(display_page);
    QGroupBox* items_group = new QGroupBox(QStringLiteral("Show on screen"), display_page);
    QVBoxLayout* items_layout = new QVBoxLayout(items_group);
    show_cpu_temp = new QCheckBox(QStringLiteral("CPU temperature"), items_group);
    show_cpu_load = new QCheckBox(QStringLiteral("CPU load"), items_group);
    show_gpu_temp = new QCheckBox(QStringLiteral("GPU temperature"), items_group);
    show_gpu_load = new QCheckBox(QStringLiteral("GPU load"), items_group);
    show_clock = new QCheckBox(QStringLiteral("Clock"), items_group);
    show_cpu_temp->setChecked(true);
    show_cpu_load->setChecked(true);
    show_gpu_temp->setChecked(true);
    show_gpu_load->setChecked(true);
    show_clock->setChecked(true);
    items_layout->addWidget(show_cpu_temp);
    items_layout->addWidget(show_cpu_load);
    items_layout->addWidget(show_gpu_temp);
    items_layout->addWidget(show_gpu_load);
    items_layout->addWidget(show_clock);
    display_layout->addWidget(items_group);
    QGroupBox* image_group = new QGroupBox(QStringLiteral("Image"), display_page);
    QFormLayout* image_form = new QFormLayout(image_group);
    fit_combo = new QComboBox(image_group);
    fit_combo->addItems({QStringLiteral("Fit image"), QStringLiteral("Fill screen")});
    fit_combo->setCurrentIndex(1);
    image_form->addRow(QStringLiteral("Scale"), fit_combo);
    display_layout->addWidget(image_group);
    QGroupBox* startup_group = new QGroupBox(QStringLiteral("Startup"), display_page);
    QVBoxLayout* startup_layout = new QVBoxLayout(startup_group);
    start_with_windows_checkbox = new QCheckBox(QStringLiteral("Start screen when I sign in"), startup_group);
    startup_layout->addWidget(start_with_windows_checkbox);
    launch_lhm_with_app_checkbox = new QCheckBox(QStringLiteral("Start Libre Hardware Monitor with Windows (in tray)"), startup_group);
    startup_layout->addWidget(launch_lhm_with_app_checkbox);
    QHBoxLayout* lhm_path_row = new QHBoxLayout();
    lhm_path_edit = new QLineEdit(startup_group);
    lhm_path_edit->setReadOnly(true);
    lhm_path_edit->setPlaceholderText(QStringLiteral("Select LibreHardwareMonitor.exe"));
    browse_lhm_button = new QPushButton(QStringLiteral("Browse..."), startup_group);
    lhm_path_row->addWidget(lhm_path_edit, 1);
    lhm_path_row->addWidget(browse_lhm_button);
    startup_layout->addLayout(lhm_path_row);
    QLabel* lhm_tray_hint = new QLabel(QStringLiteral("LHM gets its own Windows startup entry, tray settings, and sensor server. Wonder Vision can also launch it when opened."), startup_group);
    lhm_tray_hint->setWordWrap(true);
    startup_layout->addWidget(lhm_tray_hint);
    display_layout->addWidget(startup_group);
    display_layout->addStretch();
    tabs->addTab(display_page, QStringLiteral("Display"));

    QWidget* overlay_page = new QWidget(tabs);
    QVBoxLayout* overlay_page_layout = new QVBoxLayout(overlay_page);
    QGroupBox* overlay_group = new QGroupBox(QStringLiteral("Position and text"), overlay_page);
    QFormLayout* overlay_form = new QFormLayout(overlay_group);
    font_size_slider = new QSlider(Qt::Horizontal, overlay_group);
    font_size_slider->setRange(18, 64);
    font_size_slider->setValue(34);
    text_x_slider = new QSlider(Qt::Horizontal, overlay_group);
    text_x_slider->setRange(0, 100);
    text_x_slider->setValue(4);
    text_y_slider = new QSlider(Qt::Horizontal, overlay_group);
    text_y_slider->setRange(0, 100);
    text_y_slider->setValue(4);
    background_opacity_slider = new QSlider(Qt::Horizontal, overlay_group);
    background_opacity_slider->setRange(0, 255);
    background_opacity_slider->setValue(170);
    font_family_combo = new QFontComboBox(overlay_group);
    font_family_combo->setCurrentFont(QFont(QStringLiteral("Segoe UI")));
    text_color_button = new QPushButton(QStringLiteral("Choose text color..."), overlay_group);
    text_color_button->setStyleSheet(QStringLiteral("background-color: white; color: black;"));
    background_color_button = new QPushButton(QStringLiteral("Choose background..."), overlay_group);
    background_color_button->setStyleSheet(QStringLiteral("background-color: black; color: white;"));
    outline_color_button = new QPushButton(QStringLiteral("Choose outline..."), overlay_group);
    outline_color_button->setStyleSheet(QStringLiteral("background-color: black; color: white;"));
    bold_text = new QCheckBox(QStringLiteral("Bold"), overlay_group);
    bold_text->setChecked(true);
    italic_text = new QCheckBox(QStringLiteral("Italic"), overlay_group);
    outline_text = new QCheckBox(QStringLiteral("Add text outline"), overlay_group);
    outline_width_slider = new QSlider(Qt::Horizontal, overlay_group);
    outline_width_slider->setRange(1, 8);
    outline_width_slider->setValue(2);
    corner_radius_slider = new QSlider(Qt::Horizontal, overlay_group);
    corner_radius_slider->setRange(0, 48);
    corner_radius_slider->setValue(12);
    text_padding_slider = new QSlider(Qt::Horizontal, overlay_group);
    text_padding_slider->setRange(0, 80);
    text_padding_slider->setValue(17);
    text_alignment_combo = new QComboBox(overlay_group);
    text_alignment_combo->addItems({QStringLiteral("Left"), QStringLiteral("Center"), QStringLiteral("Right")});
    QHBoxLayout* style_row = new QHBoxLayout();
    style_row->addWidget(bold_text);
    style_row->addWidget(italic_text);
    overlay_form->addRow(QStringLiteral("Text X"), text_x_slider);
    overlay_form->addRow(QStringLiteral("Text Y"), text_y_slider);
    overlay_form->addRow(QStringLiteral("Text size"), font_size_slider);
    overlay_form->addRow(QStringLiteral("Font"), font_family_combo);
    overlay_form->addRow(QStringLiteral("Style"), style_row);
    overlay_form->addRow(QStringLiteral("Text color"), text_color_button);
    overlay_form->addRow(QStringLiteral("Text alignment"), text_alignment_combo);
    overlay_form->addRow(QStringLiteral("Outline"), outline_text);
    overlay_form->addRow(QStringLiteral("Outline color"), outline_color_button);
    overlay_form->addRow(QStringLiteral("Outline width"), outline_width_slider);
    overlay_form->addRow(QStringLiteral("Background"), background_color_button);
    overlay_form->addRow(QStringLiteral("Background opacity"), background_opacity_slider);
    overlay_form->addRow(QStringLiteral("Corner radius"), corner_radius_slider);
    overlay_form->addRow(QStringLiteral("Text padding"), text_padding_slider);
    overlay_page_layout->addWidget(overlay_group);
    QGroupBox* custom_text_group = new QGroupBox(QStringLiteral("Rewrite the overlay text"), overlay_page);
    QVBoxLayout* custom_text_layout = new QVBoxLayout(custom_text_group);
    custom_text_edit = new QPlainTextEdit(custom_text_group);
    custom_text_edit->setPlaceholderText(QStringLiteral("Example:\nCPU {cpu_temp} °C  {cpu_load}%\nGPU {gpu_temp} °C  {gpu_load}%\n{clock}"));
    custom_text_edit->setMaximumBlockCount(12);
    custom_text_edit->setMaximumHeight(110);
    custom_text_layout->addWidget(custom_text_edit);
    QLabel* custom_text_help = new QLabel(QStringLiteral("Your text replaces the automatic CPU/GPU/clock lines. Leave it blank to use those lines. Tokens: {cpu_temp}, {cpu_load}, {gpu_temp}, {gpu_load}, {clock}."), custom_text_group);
    custom_text_help->setWordWrap(true);
    custom_text_layout->addWidget(custom_text_help);
    overlay_page_layout->addWidget(custom_text_group);
    overlay_page_layout->addStretch();
    tabs->addTab(overlay_page, QStringLiteral("Overlay"));

    QWidget* sensors_page = new QWidget(tabs);
    QVBoxLayout* sensors_page_layout = new QVBoxLayout(sensors_page);
    QGroupBox* sensors_group = new QGroupBox(QStringLiteral("Libre Hardware Monitor"), sensors_page);
    QVBoxLayout* sensors_layout = new QVBoxLayout(sensors_group);
    sensor_status = new QLabel(QStringLiteral("Connecting to LHM at 127.0.0.1:8085..."), sensors_group);
    sensor_status->setWordWrap(true);
    sensors_layout->addWidget(sensor_status);
    QFormLayout* sensors_form = new QFormLayout();
    cpu_temp_combo = new QComboBox(sensors_group);
    cpu_load_combo = new QComboBox(sensors_group);
    gpu_temp_combo = new QComboBox(sensors_group);
    gpu_load_combo = new QComboBox(sensors_group);
    sensors_form->addRow(QStringLiteral("CPU temp"), cpu_temp_combo);
    sensors_form->addRow(QStringLiteral("CPU load"), cpu_load_combo);
    sensors_form->addRow(QStringLiteral("GPU temp"), gpu_temp_combo);
    sensors_form->addRow(QStringLiteral("GPU load"), gpu_load_combo);
    sensors_layout->addLayout(sensors_form);
    QPushButton* refresh_button = new QPushButton(QStringLiteral("Refresh sensors"), sensors_group);
    sensors_layout->addWidget(refresh_button);
    sensors_page_layout->addWidget(sensors_group);
    sensors_page_layout->addStretch();
    tabs->addTab(sensors_page, QStringLiteral("Sensors"));
    controls_layout->addWidget(tabs, 1);
    controls_scroll->setWidget(controls);

    QSettings startup_settings(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"), QSettings::NativeFormat);
    start_with_windows_checkbox->setChecked(startup_settings.contains(QStringLiteral("WonderVisionStudio")));
    QSettings app_settings;
    launch_lhm_with_app_checkbox->setChecked(app_settings.value(QStringLiteral("launchLhmWithApp"), false).toBool());
    lhm_path_edit->setText(app_settings.value(QStringLiteral("lhmExecutablePath")).toString());

    splitter->addWidget(preview);
    splitter->addWidget(controls_scroll);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({700, 420});
    root->addWidget(splitter);
    setCentralWidget(central);

    connect(open_button, &QPushButton::clicked, this, &MainWindow::OpenMedia);
    connect(start_button, &QPushButton::clicked, this, &MainWindow::StartScreen);
    connect(stop_button, &QPushButton::clicked, this, &MainWindow::StopScreen);
    connect(refresh_button, &QPushButton::clicked, this, &MainWindow::RefreshSensorCatalog);
    connect(save_config_button, &QPushButton::clicked, this, &MainWindow::SaveConfiguration);
    connect(load_config_button, &QPushButton::clicked, this, &MainWindow::LoadConfiguration);
    connect(start_with_windows_checkbox, &QCheckBox::toggled, this, &MainWindow::OnStartWithWindowsChanged);
    connect(launch_lhm_with_app_checkbox, &QCheckBox::toggled, this, &MainWindow::OnLaunchLhmChanged);
    connect(browse_lhm_button, &QPushButton::clicked, this, &MainWindow::BrowseLhmExecutable);
    connect(cpu_temp_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::OnSensorSelectionChanged);
    connect(cpu_load_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::OnSensorSelectionChanged);
    connect(gpu_temp_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::OnSensorSelectionChanged);
    connect(gpu_load_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::OnSensorSelectionChanged);
    connect(fit_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]()
    {
        media_scale_dirty = true;
        output_dirty = true;
    });
    const auto mark_output_dirty = [this]() { output_dirty = true; };
    connect(text_x_slider, &QSlider::valueChanged, this, mark_output_dirty);
    connect(text_y_slider, &QSlider::valueChanged, this, mark_output_dirty);
    connect(font_size_slider, &QSlider::valueChanged, this, mark_output_dirty);
    connect(background_opacity_slider, &QSlider::valueChanged, this, mark_output_dirty);
    connect(outline_width_slider, &QSlider::valueChanged, this, mark_output_dirty);
    connect(corner_radius_slider, &QSlider::valueChanged, this, mark_output_dirty);
    connect(text_padding_slider, &QSlider::valueChanged, this, mark_output_dirty);
    connect(custom_text_edit, &QPlainTextEdit::textChanged, this, mark_output_dirty);
    connect(font_family_combo, &QFontComboBox::currentFontChanged, this, mark_output_dirty);
    connect(text_alignment_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, mark_output_dirty);
    connect(bold_text, &QCheckBox::toggled, this, mark_output_dirty);
    connect(italic_text, &QCheckBox::toggled, this, mark_output_dirty);
    connect(outline_text, &QCheckBox::toggled, this, mark_output_dirty);
    connect(text_color_button, &QPushButton::clicked, this, [this]()
    {
        const QColor selected = QColorDialog::getColor(overlay_text_color, this, QStringLiteral("Overlay text color"));
        if(selected.isValid())
        {
            overlay_text_color = selected;
            text_color_button->setStyleSheet(QStringLiteral("background-color: %1; color: %2;")
                .arg(selected.name(), selected.lightness() < 128 ? QStringLiteral("white") : QStringLiteral("black")));
            output_dirty = true;
        }
    });
    connect(background_color_button, &QPushButton::clicked, this, [this]()
    {
        const QColor selected = QColorDialog::getColor(overlay_background_color, this, QStringLiteral("Overlay background color"));
        if(selected.isValid())
        {
            overlay_background_color = selected;
            background_color_button->setStyleSheet(QStringLiteral("background-color: %1; color: %2;")
                .arg(selected.name(), selected.lightness() < 128 ? QStringLiteral("white") : QStringLiteral("black")));
            output_dirty = true;
        }
    });
    connect(outline_color_button, &QPushButton::clicked, this, [this]()
    {
        const QColor selected = QColorDialog::getColor(overlay_outline_color, this, QStringLiteral("Text outline color"));
        if(selected.isValid())
        {
            overlay_outline_color = selected;
            outline_color_button->setStyleSheet(QStringLiteral("background-color: %1; color: %2;")
                .arg(selected.name(), selected.lightness() < 128 ? QStringLiteral("white") : QStringLiteral("black")));
            output_dirty = true;
        }
    });
    connect(show_cpu_temp, &QCheckBox::toggled, this, mark_output_dirty);
    connect(show_cpu_load, &QCheckBox::toggled, this, mark_output_dirty);
    connect(show_gpu_temp, &QCheckBox::toggled, this, mark_output_dirty);
    connect(show_gpu_load, &QCheckBox::toggled, this, mark_output_dirty);
    connect(show_clock, &QCheckBox::toggled, this, mark_output_dirty);

    tray_icon = new QSystemTrayIcon(style()->standardIcon(QStyle::SP_ComputerIcon), this);
    tray_icon->setToolTip(QStringLiteral("Wonder Vision Studio"));
    QMenu* tray_menu = new QMenu(this);
    QAction* show_action = tray_menu->addAction(QStringLiteral("Open Wonder Vision Studio"));
    tray_menu->addSeparator();
    QAction* start_action = tray_menu->addAction(QStringLiteral("Start Screen"));
    QAction* stop_action = tray_menu->addAction(QStringLiteral("Stop Screen"));
    tray_menu->addSeparator();
    QAction* quit_action = tray_menu->addAction(QStringLiteral("Quit"));
    connect(show_action, &QAction::triggered, this, [this]()
    {
        showNormal();
        raise();
        activateWindow();
    });
    connect(start_action, &QAction::triggered, this, &MainWindow::StartScreen);
    connect(stop_action, &QAction::triggered, this, &MainWindow::StopScreen);
    connect(quit_action, &QAction::triggered, this, [this]()
    {
        quit_requested = true;
        qApp->quit();
    });
    tray_icon->setContextMenu(tray_menu);
    connect(tray_icon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason)
    {
        if(reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
        {
            showNormal();
            raise();
            activateWindow();
        }
    });
    tray_icon->show();
}

void MainWindow::OpenMedia()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Open media"), QString(),
        QStringLiteral("Images and animations (*.gif *.png *.jpg *.jpeg *.bmp *.webp);;All files (*.*)"));
    if(path.isEmpty()) return;
    LoadMedia(path);
}

bool MainWindow::LoadMedia(const QString& path)
{
    if(movie)
    {
        movie->stop();
        delete movie;
        movie = nullptr;
    }
    std::unique_ptr<QImageReader> new_reader(new QImageReader(path));
    new_reader->setAutoTransform(true);
    QImage first = new_reader->read();
    if(first.isNull())
    {
        SetUiStatus(QStringLiteral("Could not open that image or GIF."));
        return false;
    }

    const int frame_count = new_reader->imageCount();
    animated = new_reader->supportsAnimation() && frame_count != 1;
    if(animated)
    {
        QMovie* new_movie = new QMovie(path, QByteArray(), this);
        new_movie->setCacheMode(QMovie::CacheAll);
        if(!new_movie->isValid())
        {
            delete new_movie;
            animated = false;
            reader = std::move(new_reader);
        }
        else
        {
            movie = new_movie;
            reader.reset();
            connect(movie, &QMovie::frameChanged, this, [this](int)
            {
                if(!movie) return;
                const QImage frame = movie->currentImage();
                if(!frame.isNull())
                {
                    media_frame = frame;
                    media_scale_dirty = true;
                }
            });
            connect(movie, &QMovie::finished, this, [this]()
            {
                if(movie) movie->start();
            });
            movie->start();
        }
    }
    else
    {
        reader = std::move(new_reader);
    }
    media_path = path;
    media_frame = first;
    media_scale_dirty = true;
    output_dirty = true;
    media_name->setText(QFileInfo(path).fileName() + (animated ? QStringLiteral("  •  animated GIF") : QString()));
    SetUiStatus(QStringLiteral("Loaded %1 × %2 image%3.").arg(first.width()).arg(first.height()).arg(animated ? QStringLiteral(" (animated)") : QString()));
    Tick();
    return true;
}

void MainWindow::SaveConfiguration()
{
    QString suggested_path = last_config_path;
    if(suggested_path.isEmpty())
    {
        const QString config_dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        QDir().mkpath(config_dir);
        suggested_path = QDir(config_dir).filePath(QStringLiteral("WonderVisionStudio.ini"));
    }
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save configuration"), suggested_path,
        QStringLiteral("Wonder Vision config (*.ini)"));
    if(path.isEmpty()) return;
    QString config_path = path;
    if(QFileInfo(config_path).suffix().isEmpty()) config_path += QStringLiteral(".ini");
    if(SaveConfigurationToPath(config_path, true))
    {
        SetUiStatus(QStringLiteral("Configuration saved. It will load the next time you open the app."));
    }
}

void MainWindow::LoadConfiguration()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Load configuration"), last_config_path,
        QStringLiteral("Wonder Vision config (*.ini);;All files (*.*)"));
    if(path.isEmpty()) return;
    if(LoadConfigurationFromPath(path))
    {
        QSettings app_settings;
        app_settings.setValue(QStringLiteral("lastConfigPath"), QFileInfo(path).absoluteFilePath());
        app_settings.sync();
        last_config_path = QFileInfo(path).absoluteFilePath();
        SetUiStatus(QStringLiteral("Configuration loaded. It will load automatically next time."));
    }
}

bool MainWindow::SaveConfigurationToPath(const QString& path, bool remember_path)
{
    QSettings config(path, QSettings::IniFormat);
    config.clear();
    config.setValue(QStringLiteral("version"), 1);
    config.setValue(QStringLiteral("media/path"), media_path);
    config.setValue(QStringLiteral("display/showCpuTemperature"), show_cpu_temp->isChecked());
    config.setValue(QStringLiteral("display/showCpuLoad"), show_cpu_load->isChecked());
    config.setValue(QStringLiteral("display/showGpuTemperature"), show_gpu_temp->isChecked());
    config.setValue(QStringLiteral("display/showGpuLoad"), show_gpu_load->isChecked());
    config.setValue(QStringLiteral("display/showClock"), show_clock->isChecked());
    config.setValue(QStringLiteral("display/fitMode"), fit_combo->currentIndex());
    config.setValue(QStringLiteral("sensors/cpuTemperature"), cpu_temp_combo->currentData().toString());
    config.setValue(QStringLiteral("sensors/cpuLoad"), cpu_load_combo->currentData().toString());
    config.setValue(QStringLiteral("sensors/gpuTemperature"), gpu_temp_combo->currentData().toString());
    config.setValue(QStringLiteral("sensors/gpuLoad"), gpu_load_combo->currentData().toString());
    config.setValue(QStringLiteral("overlay/x"), text_x_slider->value());
    config.setValue(QStringLiteral("overlay/y"), text_y_slider->value());
    config.setValue(QStringLiteral("overlay/size"), font_size_slider->value());
    config.setValue(QStringLiteral("overlay/fontFamily"), font_family_combo->currentFont().family());
    config.setValue(QStringLiteral("overlay/bold"), bold_text->isChecked());
    config.setValue(QStringLiteral("overlay/italic"), italic_text->isChecked());
    config.setValue(QStringLiteral("overlay/textColor"), overlay_text_color.name(QColor::HexArgb));
    config.setValue(QStringLiteral("overlay/alignment"), text_alignment_combo->currentIndex());
    config.setValue(QStringLiteral("overlay/outlineEnabled"), outline_text->isChecked());
    config.setValue(QStringLiteral("overlay/outlineColor"), overlay_outline_color.name(QColor::HexArgb));
    config.setValue(QStringLiteral("overlay/outlineWidth"), outline_width_slider->value());
    config.setValue(QStringLiteral("overlay/backgroundColor"), overlay_background_color.name(QColor::HexArgb));
    config.setValue(QStringLiteral("overlay/backgroundOpacity"), background_opacity_slider->value());
    config.setValue(QStringLiteral("overlay/cornerRadius"), corner_radius_slider->value());
    config.setValue(QStringLiteral("overlay/padding"), text_padding_slider->value());
    config.setValue(QStringLiteral("overlay/customText"), custom_text_edit->toPlainText());
    config.sync();
    if(config.status() != QSettings::NoError)
    {
        QMessageBox::warning(this, QStringLiteral("Save failed"), QStringLiteral("Could not write the config file."));
        return false;
    }
    if(remember_path)
    {
        QSettings app_settings;
        last_config_path = QFileInfo(path).absoluteFilePath();
        app_settings.setValue(QStringLiteral("lastConfigPath"), last_config_path);
        app_settings.sync();
        if(app_settings.status() != QSettings::NoError)
        {
            QMessageBox::warning(this, QStringLiteral("Saved"), QStringLiteral("The config file was saved, but its path could not be remembered for startup."));
        }
    }
    return true;
}

bool MainWindow::LoadConfigurationFromPath(const QString& path)
{
    if(!QFileInfo::exists(path))
    {
        SetUiStatus(QStringLiteral("The saved config or one of its files could not be found."));
        return false;
    }
    QSettings config(path, QSettings::IniFormat);
    if(config.status() != QSettings::NoError)
    {
        QMessageBox::warning(this, QStringLiteral("Load failed"), QStringLiteral("Could not read that config file."));
        return false;
    }
    show_cpu_temp->setChecked(config.value(QStringLiteral("display/showCpuTemperature"), true).toBool());
    show_cpu_load->setChecked(config.value(QStringLiteral("display/showCpuLoad"), true).toBool());
    show_gpu_temp->setChecked(config.value(QStringLiteral("display/showGpuTemperature"), true).toBool());
    show_gpu_load->setChecked(config.value(QStringLiteral("display/showGpuLoad"), true).toBool());
    show_clock->setChecked(config.value(QStringLiteral("display/showClock"), true).toBool());
    fit_combo->setCurrentIndex(qBound(0, config.value(QStringLiteral("display/fitMode"), 1).toInt(), fit_combo->count() - 1));
    text_x_slider->setValue(config.value(QStringLiteral("overlay/x"), 4).toInt());
    text_y_slider->setValue(config.value(QStringLiteral("overlay/y"), 4).toInt());
    font_size_slider->setValue(config.value(QStringLiteral("overlay/size"), 34).toInt());
    font_family_combo->setCurrentFont(QFont(config.value(QStringLiteral("overlay/fontFamily"), QStringLiteral("Segoe UI")).toString()));
    bold_text->setChecked(config.value(QStringLiteral("overlay/bold"), true).toBool());
    italic_text->setChecked(config.value(QStringLiteral("overlay/italic"), false).toBool());
    text_alignment_combo->setCurrentIndex(qBound(0, config.value(QStringLiteral("overlay/alignment"), 0).toInt(), text_alignment_combo->count() - 1));
    outline_text->setChecked(config.value(QStringLiteral("overlay/outlineEnabled"), false).toBool());
    outline_width_slider->setValue(config.value(QStringLiteral("overlay/outlineWidth"), 2).toInt());
    background_opacity_slider->setValue(config.value(QStringLiteral("overlay/backgroundOpacity"), 170).toInt());
    corner_radius_slider->setValue(config.value(QStringLiteral("overlay/cornerRadius"), 12).toInt());
    text_padding_slider->setValue(config.value(QStringLiteral("overlay/padding"), 17).toInt());
    custom_text_edit->setPlainText(config.value(QStringLiteral("overlay/customText")).toString());

    const QColor text_color(config.value(QStringLiteral("overlay/textColor"), QStringLiteral("#ffffffff")).toString());
    const QColor background_color(config.value(QStringLiteral("overlay/backgroundColor"), QStringLiteral("#ff000000")).toString());
    const QColor outline_color(config.value(QStringLiteral("overlay/outlineColor"), QStringLiteral("#ff000000")).toString());
    if(text_color.isValid()) overlay_text_color = text_color;
    if(background_color.isValid()) overlay_background_color = background_color;
    if(outline_color.isValid()) overlay_outline_color = outline_color;
    UpdateColorButton(text_color_button, overlay_text_color);
    UpdateColorButton(background_color_button, overlay_background_color);
    UpdateColorButton(outline_color_button, overlay_outline_color);

    pending_cpu_temp_id = config.value(QStringLiteral("sensors/cpuTemperature")).toString();
    pending_cpu_load_id = config.value(QStringLiteral("sensors/cpuLoad")).toString();
    pending_gpu_temp_id = config.value(QStringLiteral("sensors/gpuTemperature")).toString();
    pending_gpu_load_id = config.value(QStringLiteral("sensors/gpuLoad")).toString();
    auto select_sensor = [](QComboBox* combo, const QString& id)
    {
        const int index = id.isEmpty() ? -1 : combo->findData(id);
        if(index >= 0)
        {
            combo->setCurrentIndex(index);
        }
    };
    select_sensor(cpu_temp_combo, pending_cpu_temp_id);
    select_sensor(cpu_load_combo, pending_cpu_load_id);
    select_sensor(gpu_temp_combo, pending_gpu_temp_id);
    select_sensor(gpu_load_combo, pending_gpu_load_id);
    OnSensorSelectionChanged();

    if(movie)
    {
        movie->stop();
        delete movie;
        movie = nullptr;
    }
    reader.reset();
    media_frame = QImage();
    scaled_media_frame = QImage();
    composed_frame = QImage();
    media_path.clear();
    animated = false;
    media_name->setText(QStringLiteral("No file selected"));
    output_dirty = true;
    media_scale_dirty = true;
    const QString saved_media_path = config.value(QStringLiteral("media/path")).toString();
    if(!saved_media_path.isEmpty() && !LoadMedia(saved_media_path)) return false;
    Tick();
    return true;
}

void MainWindow::LoadLastConfiguration()
{
    QSettings app_settings;
    last_config_path = app_settings.value(QStringLiteral("lastConfigPath")).toString();
    if(!last_config_path.isEmpty() && QFileInfo::exists(last_config_path))
    {
        LoadConfigurationFromPath(last_config_path);
    }
}

void MainWindow::UpdateColorButton(QPushButton* button, const QColor& color)
{
    const QString foreground = color.lightness() < 128 ? QStringLiteral("white") : QStringLiteral("black");
    button->setStyleSheet(QStringLiteral("background-color: %1; color: %2;").arg(color.name(), foreground));
}

void MainWindow::OnStartWithWindowsChanged(bool enabled)
{
    QSettings startup_settings(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"), QSettings::NativeFormat);
    if(enabled)
    {
        QString config_path = last_config_path;
        if(config_path.isEmpty())
        {
            const QString config_dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
            QDir().mkpath(config_dir);
            config_path = QDir(config_dir).filePath(QStringLiteral("WonderVisionStudio.ini"));
        }
        if(!SaveConfigurationToPath(config_path, true))
        {
            start_with_windows_checkbox->blockSignals(true);
            start_with_windows_checkbox->setChecked(false);
            start_with_windows_checkbox->blockSignals(false);
            return;
        }
        const QString command = QStringLiteral("\"%1\" --start-screen --minimize-to-tray")
            .arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
        startup_settings.setValue(QStringLiteral("WonderVisionStudio"), command);
    }
    else
    {
        startup_settings.remove(QStringLiteral("WonderVisionStudio"));
    }
    startup_settings.sync();
    if(startup_settings.status() != QSettings::NoError)
    {
        start_with_windows_checkbox->blockSignals(true);
        start_with_windows_checkbox->setChecked(!enabled);
        start_with_windows_checkbox->blockSignals(false);
        SetUiStatus(QStringLiteral("Windows startup setting could not be changed."));
        return;
    }
    SetUiStatus(enabled ? QStringLiteral("Wonder Vision will start with Windows using the saved config.")
                        : QStringLiteral("Wonder Vision will no longer start with Windows."));
}

void MainWindow::RefreshStartupRegistration()
{
    if(!start_with_windows_checkbox->isChecked()) return;
    QSettings startup_settings(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"), QSettings::NativeFormat);
    const QString command = QStringLiteral("\"%1\" --start-screen --minimize-to-tray")
        .arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
    startup_settings.setValue(QStringLiteral("WonderVisionStudio"), command);
    startup_settings.sync();
}

void MainWindow::OnLaunchLhmChanged(bool enabled)
{
    if(enabled && !QFileInfo(lhm_path_edit->text()).isFile())
    {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Choose Libre Hardware Monitor"),
            lhm_path_edit->text(), QStringLiteral("Libre Hardware Monitor (LibreHardwareMonitor.exe);;Applications (*.exe)"));
        if(path.isEmpty())
        {
            launch_lhm_with_app_checkbox->blockSignals(true);
            launch_lhm_with_app_checkbox->setChecked(false);
            launch_lhm_with_app_checkbox->blockSignals(false);
            return;
        }
        lhm_path_edit->setText(QFileInfo(path).absoluteFilePath());
    }

    if(enabled)
    {
        QString config_error;
        if(!PrepareLhmForTrayAndSensors(lhm_path_edit->text(), config_error))
        {
            launch_lhm_with_app_checkbox->blockSignals(true);
            launch_lhm_with_app_checkbox->setChecked(false);
            launch_lhm_with_app_checkbox->blockSignals(false);
            SetUiStatus(config_error);
            return;
        }
    }

    if(!UpdateLhmStartupRegistration(enabled))
    {
        launch_lhm_with_app_checkbox->blockSignals(true);
        launch_lhm_with_app_checkbox->setChecked(!enabled);
        launch_lhm_with_app_checkbox->blockSignals(false);
        SetUiStatus(QStringLiteral("Windows could not change Libre Hardware Monitor startup."));
        return;
    }

    QSettings app_settings;
    app_settings.setValue(QStringLiteral("launchLhmWithApp"), enabled);
    app_settings.setValue(QStringLiteral("lhmExecutablePath"), lhm_path_edit->text());
    app_settings.sync();
    if(enabled) LaunchLhm();
    else SetUiStatus(QStringLiteral("Libre Hardware Monitor will no longer start with Windows."));
}

void MainWindow::BrowseLhmExecutable()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Choose Libre Hardware Monitor"),
        lhm_path_edit->text(), QStringLiteral("Libre Hardware Monitor (LibreHardwareMonitor.exe);;Applications (*.exe)"));
    if(path.isEmpty()) return;
    lhm_path_edit->setText(QFileInfo(path).absoluteFilePath());
    QSettings app_settings;
    app_settings.setValue(QStringLiteral("lhmExecutablePath"), lhm_path_edit->text());
    app_settings.sync();
    if(launch_lhm_with_app_checkbox->isChecked()) OnLaunchLhmChanged(true);
}

bool MainWindow::UpdateLhmStartupRegistration(bool enabled)
{
    QSettings startup_settings(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"), QSettings::NativeFormat);
    if(enabled)
    {
        const QString command = QStringLiteral("\"%1\"").arg(QDir::toNativeSeparators(QFileInfo(lhm_path_edit->text()).absoluteFilePath()));
        startup_settings.setValue(QStringLiteral("LibreHardwareMonitor"), command);
    }
    else
    {
        startup_settings.remove(QStringLiteral("LibreHardwareMonitor"));
    }
    startup_settings.sync();
    return startup_settings.status() == QSettings::NoError;
}

void MainWindow::LaunchLhm()
{
    const QString executable = QFileInfo(lhm_path_edit->text()).absoluteFilePath();
    if(!QFileInfo(executable).isFile())
    {
        SetUiStatus(QStringLiteral("Choose LibreHardwareMonitor.exe in the Display tab to launch it."));
        return;
    }

    DWORD existing_process_id = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if(snapshot != INVALID_HANDLE_VALUE)
    {
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if(Process32FirstW(snapshot, &entry))
        {
            do
            {
                if(QString::fromWCharArray(entry.szExeFile).compare(QStringLiteral("LibreHardwareMonitor.exe"), Qt::CaseInsensitive) == 0)
                {
                    existing_process_id = entry.th32ProcessID;
                    break;
                }
            } while(Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
    }
    if(existing_process_id != 0)
    {
        MainWindowSearch search{existing_process_id, nullptr};
        EnumWindows(FindProcessMainWindow, reinterpret_cast<LPARAM>(&search));
        if(search.window) PostMessageW(search.window, WM_SYSCOMMAND, SC_MINIMIZE, 0);
        SetUiStatus(QStringLiteral("Libre Hardware Monitor was already open; minimize was requested. If it stays on the taskbar, close it once so Wonder Vision can relaunch it with tray settings."));
        return;
    }

    QString config_error;
    if(!PrepareLhmForTrayAndSensors(executable, config_error))
    {
        SetUiStatus(config_error);
        return;
    }

    const QString working_directory = QFileInfo(executable).absolutePath();
    SHELLEXECUTEINFOW launch{};
    launch.cbSize = sizeof(launch);
    launch.fMask = SEE_MASK_NOCLOSEPROCESS;
    launch.lpFile = reinterpret_cast<LPCWSTR>(executable.utf16());
    launch.lpDirectory = reinterpret_cast<LPCWSTR>(working_directory.utf16());
    launch.nShow = SW_SHOWNORMAL;
    if(!ShellExecuteExW(&launch))
    {
        SetUiStatus(QStringLiteral("Could not launch Libre Hardware Monitor (Windows error %1).")
            .arg(static_cast<unsigned int>(GetLastError())));
        return;
    }
    if(launch.hProcess) CloseHandle(launch.hProcess);
    SetUiStatus(QStringLiteral("Libre Hardware Monitor launched in the system tray with its sensor web server enabled."));
}

void MainWindow::RetryStartupScreen()
{
    if(!startup_screen_pending)
    {
        startup_retry_timer->stop();
        return;
    }
    const QString status = device.Status();
    if(status.startsWith(QStringLiteral("Connected"), Qt::CaseInsensitive))
    {
        startup_screen_pending = false;
        startup_retry_timer->stop();
        return;
    }
    if(status.startsWith(QStringLiteral("Connecting"), Qt::CaseInsensitive)) return;
    if(startup_retry_count >= 12)
    {
        startup_screen_pending = false;
        startup_retry_timer->stop();
        SetUiStatus(QStringLiteral("Could not connect to Wonder Vision after several startup attempts."));
        return;
    }
    ++startup_retry_count;
    device.Stop();
    device.SetFrame(ComposeFrame());
    device.Start();
}

void MainWindow::StartScreen()
{
    if(media_frame.isNull())
    {
        SetUiStatus(QStringLiteral("Open an image or GIF first."));
        return;
    }
    device.SetFrame(ComposeFrame());
    device.Start();
    start_button->setEnabled(false);
    stop_button->setEnabled(true);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if(!quit_requested && tray_icon && tray_icon->isVisible())
    {
        hide();
        event->ignore();
        tray_icon->showMessage(QStringLiteral("Wonder Vision Studio"),
            QStringLiteral("Still running in the system tray."), QSystemTrayIcon::Information, 2500);
        return;
    }
    event->accept();
}

void MainWindow::StopScreen()
{
    device.Stop();
    start_button->setEnabled(true);
    stop_button->setEnabled(false);
}

void MainWindow::RefreshSensorCatalog()
{
    refresh_sensors_requested = true;
    sensor_status->setText(QStringLiteral("Refreshing sensor list..."));
}

void MainWindow::OnSensorSelectionChanged()
{
    if(sender() == cpu_temp_combo) pending_cpu_temp_id.clear();
    else if(sender() == cpu_load_combo) pending_cpu_load_id.clear();
    else if(sender() == gpu_temp_combo) pending_gpu_temp_id.clear();
    else if(sender() == gpu_load_combo) pending_gpu_load_id.clear();
    std::lock_guard<std::mutex> lock(sensor_mutex);
    sensor_bindings.cpu_temp_sensor_id = cpu_temp_combo->currentData().toString().toStdString();
    sensor_bindings.cpu_load_sensor_id = cpu_load_combo->currentData().toString().toStdString();
    sensor_bindings.gpu_temp_sensor_id = gpu_temp_combo->currentData().toString().toStdString();
    sensor_bindings.gpu_load_sensor_id = gpu_load_combo->currentData().toString().toStdString();
    output_dirty = true;
}

void MainWindow::SensorLoop()
{
    ThermalrightLhmReader reader_instance;
    bool catalog_loaded = false;
    int retry_wait = 0;
    while(!sensor_stop)
    {
        const bool refresh_requested = refresh_sensors_requested.exchange(false);
        if(refresh_requested || (!catalog_loaded && retry_wait <= 0))
        {
            ThermalrightLhmSensorCatalog catalog;
            const bool ok = reader_instance.FetchSensorCatalog(catalog);
            {
                std::lock_guard<std::mutex> lock(sensor_mutex);
                if(ok) sensor_catalog = std::move(catalog);
                sensor_catalog_ok = ok;
            }
            sensor_catalog_updated = true;
            catalog_loaded = ok;
            retry_wait = ok ? 0 : 50;
        }

        if(catalog_loaded)
        {
            ThermalrightLhmSensorBindings bindings;
            {
                std::lock_guard<std::mutex> lock(sensor_mutex);
                bindings = sensor_bindings;
            }
            ThermalrightSystemMetrics current;
            reader_instance.TryRead(bindings, current);
            {
                std::lock_guard<std::mutex> lock(sensor_mutex);
                metrics = current;
            }
        }

        for(int i = 0; i < 10 && !sensor_stop; ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if(retry_wait > 0) retry_wait -= 1;
    }
    reader_instance.Shutdown();
}

void MainWindow::PopulateSensorCombo(QComboBox* combo, const std::vector<ThermalrightLhmSensorOption>& sensors, QString& preferred_id)
{
    const QString previous = combo->currentData().toString();
    combo->blockSignals(true);
    combo->clear();
    for(const auto& sensor : sensors)
    {
        combo->addItem(QString::fromStdString(sensor.label), QString::fromStdString(sensor.sensor_id));
    }
    int index = preferred_id.isEmpty() ? -1 : combo->findData(preferred_id);
    if(index >= 0) preferred_id.clear();
    if(index < 0) index = previous.isEmpty() ? -1 : combo->findData(previous);
    if(index < 0 && combo->count() > 0) index = 0;
    if(index >= 0) combo->setCurrentIndex(index);
    combo->blockSignals(false);
}

void MainWindow::ApplySensorCatalog()
{
    if(!sensor_catalog_updated.exchange(false)) return;
    ThermalrightLhmSensorCatalog catalog;
    bool ok = false;
    {
        std::lock_guard<std::mutex> lock(sensor_mutex);
        catalog = sensor_catalog;
        ok = sensor_catalog_ok;
    }
    if(!ok)
    {
        sensor_status->setText(QStringLiteral("LHM not found. Run Libre Hardware Monitor and enable its Remote Web Server on port 8085."));
        return;
    }
    PopulateSensorCombo(cpu_temp_combo, catalog.cpu_temp_sensors, pending_cpu_temp_id);
    PopulateSensorCombo(cpu_load_combo, catalog.cpu_load_sensors, pending_cpu_load_id);
    PopulateSensorCombo(gpu_temp_combo, catalog.gpu_temp_sensors, pending_gpu_temp_id);
    PopulateSensorCombo(gpu_load_combo, catalog.gpu_load_sensors, pending_gpu_load_id);
    OnSensorSelectionChanged();
    sensor_status->setText(QStringLiteral("Live LHM sensors connected."));
}

QString MainWindow::OverlayText() const
{
    ThermalrightSystemMetrics current;
    {
        std::lock_guard<std::mutex> lock(sensor_mutex);
        current = metrics;
    }
    QString custom_text = custom_text_edit->toPlainText();
    if(!custom_text.trimmed().isEmpty())
    {
        custom_text.replace(QStringLiteral("{cpu_temp}"), Metric(current.cpu_temp_c, current.cpu_temp_valid, QStringLiteral("")));
        custom_text.replace(QStringLiteral("{cpu_load}"), Metric(current.cpu_load_pct, current.cpu_load_valid, QStringLiteral("")));
        custom_text.replace(QStringLiteral("{gpu_temp}"), Metric(current.gpu_temp_c, current.gpu_temp_valid, QStringLiteral("")));
        custom_text.replace(QStringLiteral("{gpu_load}"), Metric(current.gpu_load_pct, current.gpu_load_valid, QStringLiteral("")));
        custom_text.replace(QStringLiteral("{clock}"), QDateTime::currentDateTime().toString(QStringLiteral("ddd  MMM d   hh:mm:ss")));
        return custom_text;
    }

    QStringList lines;
    if(show_cpu_temp->isChecked() || show_cpu_load->isChecked())
    {
        QStringList values;
        if(show_cpu_temp->isChecked()) values << Metric(current.cpu_temp_c, current.cpu_temp_valid, QStringLiteral("°C"));
        if(show_cpu_load->isChecked()) values << Metric(current.cpu_load_pct, current.cpu_load_valid, QStringLiteral("%"));
        lines << QStringLiteral("CPU   %1").arg(values.join(QStringLiteral("   ")));
    }
    if(show_gpu_temp->isChecked() || show_gpu_load->isChecked())
    {
        QStringList values;
        if(show_gpu_temp->isChecked()) values << Metric(current.gpu_temp_c, current.gpu_temp_valid, QStringLiteral("°C"));
        if(show_gpu_load->isChecked()) values << Metric(current.gpu_load_pct, current.gpu_load_valid, QStringLiteral("%"));
        lines << QStringLiteral("GPU   %1").arg(values.join(QStringLiteral("   ")));
    }
    if(show_clock->isChecked())
    {
        lines << QDateTime::currentDateTime().toString(QStringLiteral("ddd  MMM d   hh:mm:ss"));
    }
    return lines.join(QLatin1Char('\n'));
}

QImage MainWindow::ComposeFrame()
{
    const QSize size = device.Resolution();
    if(media_scale_dirty || scaled_media_size != size)
    {
        QImage base(size, QImage::Format_RGB32);
        base.fill(QColor(4, 8, 15));
        if(!media_frame.isNull())
        {
            const Qt::AspectRatioMode mode = fit_combo->currentIndex() == 0
                ? Qt::KeepAspectRatio : Qt::KeepAspectRatioByExpanding;
            const QImage scaled = media_frame.scaled(size, mode, Qt::SmoothTransformation);
            if(fit_combo->currentIndex() == 0)
            {
                QPainter painter(&base);
                painter.drawImage((size.width() - scaled.width()) / 2, (size.height() - scaled.height()) / 2, scaled);
            }
            else
            {
                const int x = (scaled.width() - size.width()) / 2;
                const int y = (scaled.height() - size.height()) / 2;
                base = scaled.copy(x, y, size.width(), size.height());
            }
        }
        scaled_media_frame = base;
        scaled_media_size = size;
        media_scale_dirty = false;
    }

    QImage canvas = scaled_media_frame;

    const QString text = OverlayText();
    if(!text.isEmpty())
    {
        QPainter painter(&canvas);
        painter.setRenderHint(QPainter::Antialiasing, true);
        QFont font = font_family_combo->currentFont();
        font.setPixelSize(font_size_slider->value());
        font.setBold(bold_text->isChecked());
        font.setItalic(italic_text->isChecked());
        painter.setFont(font);
        QFontMetrics fm(font);
        const QRect text_bounds = fm.boundingRect(QRect(0, 0, size.width() - 64, size.height() - 64),
                                                    Qt::AlignLeft | Qt::TextWordWrap, text);
        const int padding = text_padding_slider->value();
        const int box_width = qMin(size.width() - 40, text_bounds.width() + padding * 2);
        const int box_height = text_bounds.height() + padding * 2;
        const int max_x = qMax(0, size.width() - box_width);
        const int max_y = qMax(0, size.height() - box_height);
        QRect box(text_x_slider->value() * max_x / 100,
                  text_y_slider->value() * max_y / 100, box_width, box_height);
        painter.setPen(Qt::NoPen);
        QColor background = overlay_background_color;
        background.setAlpha(background_opacity_slider->value());
        painter.setBrush(background);
        painter.drawRoundedRect(box, corner_radius_slider->value(), corner_radius_slider->value());
        const QRect text_rect = box.adjusted(padding, padding, -padding, -padding);
        Qt::Alignment alignment = Qt::AlignLeft;
        if(text_alignment_combo->currentIndex() == 1) alignment = Qt::AlignHCenter;
        if(text_alignment_combo->currentIndex() == 2) alignment = Qt::AlignRight;
        const int text_flags = static_cast<int>(alignment) | static_cast<int>(Qt::AlignVCenter)
                             | static_cast<int>(Qt::TextWordWrap);
        if(outline_text->isChecked())
        {
            QPainterPath glyphs;
            const QStringList lines = text.split(QLatin1Char('\n'));
            const int line_height = fm.lineSpacing();
            const int total_height = line_height * lines.size();
            int baseline = text_rect.top() + qMax(fm.ascent(), (text_rect.height() - total_height) / 2 + fm.ascent());
            for(const QString& line : lines)
            {
                const int line_width = fm.horizontalAdvance(line);
                int x = text_rect.left();
                if(text_alignment_combo->currentIndex() == 1) x += (text_rect.width() - line_width) / 2;
                if(text_alignment_combo->currentIndex() == 2) x += text_rect.width() - line_width;
                glyphs.addText(x, baseline, font, line);
                baseline += line_height;
            }
            QPen outline_pen(overlay_outline_color, outline_width_slider->value() * 2.0,
                             Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            painter.setBrush(overlay_text_color);
            painter.setPen(outline_pen);
            painter.drawPath(glyphs);
        }
        else
        {
            painter.setPen(overlay_text_color);
            painter.drawText(text_rect, text_flags, text);
        }
    }
    return canvas;
}

void MainWindow::Tick()
{
    ApplySensorCatalog();
    if(media_frame.isNull()) return;
    const QString overlay_text = OverlayText();
    const bool content_changed = output_dirty || media_scale_dirty || overlay_text != last_overlay_text;
    if(content_changed)
    {
        composed_frame = ComposeFrame();
        last_overlay_text = overlay_text;
        output_dirty = false;
        device.SetFrame(composed_frame);
    }
    // Fit the 20:9 panel inside the preview bounds without distortion.
    if(!composed_frame.isNull())
    {
        preview_image->setPixmap(QPixmap::fromImage(composed_frame).scaled(preview_image->size(), Qt::KeepAspectRatio, Qt::FastTransformation));
    }
    const QString status = device.Status();
    device_status->setText(status);
    const bool running = status.startsWith(QStringLiteral("Connecting"), Qt::CaseInsensitive)
                      || status.startsWith(QStringLiteral("Connected"), Qt::CaseInsensitive);
    start_button->setEnabled(!running);
    stop_button->setEnabled(running);
}

void MainWindow::SetUiStatus(const QString& text)
{
    statusBar()->showMessage(text, 5000);
}
