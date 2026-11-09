#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

#include <QMainWindow>
#include <QColor>
#include <QImage>
#include <QImageReader>

#include "ThermalrightLhmReader.h"
#include "ThermalrightSystemMetrics.h"
#include "WonderVisionDevice.h"

class QCheckBox;
class QCloseEvent;
class QComboBox;
class QFontComboBox;
class QLabel;
class QLineEdit;
class QMovie;
class QPlainTextEdit;
class QPushButton;
class QSystemTrayIcon;
class QSlider;
class QTabWidget;
class QTimer;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(bool start_screen_on_launch = false, bool start_minimized_to_tray = false, QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void OpenMedia();
    void StartScreen();
    void StopScreen();
    void RefreshSensorCatalog();
    void OnSensorSelectionChanged();
    void Tick();
    void SaveConfiguration();
    void LoadConfiguration();
    void OnStartWithWindowsChanged(bool enabled);
    void OnLaunchLhmChanged(bool enabled);
    void BrowseLhmExecutable();
    void RetryStartupScreen();

private:
    void BuildUi();
    void SensorLoop();
    void ApplySensorCatalog();
    void PopulateSensorCombo(QComboBox* combo, const std::vector<ThermalrightLhmSensorOption>& sensors, QString& preferred_id);
    bool LoadMedia(const QString& path);
    bool SaveConfigurationToPath(const QString& path, bool remember_path);
    bool LoadConfigurationFromPath(const QString& path);
    void LoadLastConfiguration();
    void UpdateColorButton(QPushButton* button, const QColor& color);
    void LaunchLhm();
    void RefreshStartupRegistration();
    bool UpdateLhmStartupRegistration(bool enabled);
    QImage ComposeFrame();
    QString OverlayText() const;
    void SetUiStatus(const QString& text);

    QWidget* preview;
    QLabel* preview_image;
    QLabel* media_name;
    QLabel* sensor_status;
    QLabel* device_status;
    QPushButton* start_button;
    QPushButton* stop_button;
    QPushButton* save_config_button;
    QPushButton* load_config_button;
    QCheckBox* start_with_windows_checkbox;
    QComboBox* cpu_temp_combo;
    QComboBox* cpu_load_combo;
    QComboBox* gpu_temp_combo;
    QComboBox* gpu_load_combo;
    QComboBox* fit_combo;
    QCheckBox* show_cpu_temp;
    QCheckBox* show_cpu_load;
    QCheckBox* show_gpu_temp;
    QCheckBox* show_gpu_load;
    QCheckBox* show_clock;
    QCheckBox* bold_text;
    QCheckBox* italic_text;
    QCheckBox* outline_text;
    QSlider* font_size_slider;
    QSlider* text_x_slider;
    QSlider* text_y_slider;
    QSlider* background_opacity_slider;
    QSlider* outline_width_slider;
    QSlider* corner_radius_slider;
    QSlider* text_padding_slider;
    QFontComboBox* font_family_combo;
    QPushButton* text_color_button;
    QPushButton* background_color_button;
    QPushButton* outline_color_button;
    QPlainTextEdit* custom_text_edit;
    QComboBox* text_alignment_combo;
    QColor overlay_text_color;
    QColor overlay_background_color;
    QColor overlay_outline_color;
    QTimer* ui_timer;

    std::unique_ptr<QImageReader> reader;
    QMovie* movie;
    QString media_path;
    QString last_config_path;
    QString pending_cpu_temp_id;
    QString pending_cpu_load_id;
    QString pending_gpu_temp_id;
    QString pending_gpu_load_id;
    QImage media_frame;
    QImage scaled_media_frame;
    QSize scaled_media_size;
    bool media_scale_dirty;
    bool output_dirty;
    bool animated;
    QString last_overlay_text;
    QImage composed_frame;
    WonderVisionDevice device;
    std::atomic<bool> sensor_stop;
    std::atomic<bool> refresh_sensors_requested;
    std::atomic<bool> sensor_catalog_updated;
    std::thread sensor_thread;
    mutable std::mutex sensor_mutex;
    ThermalrightLhmSensorCatalog sensor_catalog;
    ThermalrightLhmSensorBindings sensor_bindings;
    ThermalrightSystemMetrics metrics;
    bool sensor_catalog_ok;
    QPushButton* browse_lhm_button;
    QCheckBox* launch_lhm_with_app_checkbox;
    QLineEdit* lhm_path_edit;
    QSystemTrayIcon* tray_icon;
    QTimer* startup_retry_timer;
    int startup_retry_count;
    bool startup_screen_pending;
    bool quit_requested;

protected:
    void closeEvent(QCloseEvent* event) override;
};
