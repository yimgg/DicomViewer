#include "widget.h"
#include "./ui_widget.h"

#if defined(_MSC_VER) && (_MSC_VER >= 1600)
# pragma execution_character_set("utf-8")
#endif

#include <QFileDialog>
#include <QMessageBox>
#include <QSlider>

#include <cstring>

// VTK 模块初始化（必须在包含其他 VTK 头文件之前）
#include <vtkAutoInit.h>
VTK_MODULE_INIT(vtkRenderingOpenGL2);
VTK_MODULE_INIT(vtkInteractionStyle);
VTK_MODULE_INIT(vtkRenderingFreeType);

#include <QVTKOpenGLNativeWidget.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkRenderer.h>

#include <itkImageSeriesReader.h>
#include <itkGDCMImageIO.h>
#include <itkGDCMSeriesFileNames.h>

Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
    , view_axial(nullptr)
    , view_sagittal(nullptr)
    , view_coronal(nullptr)
    , view_3d(nullptr)
    , renderWindow_axial(nullptr)
    , renderWindow_sagittal(nullptr)
    , renderWindow_coronal(nullptr)
    , renderWindow_3d(nullptr)
    , renderer_axial(nullptr)
    , renderer_sagittal(nullptr)
    , renderer_coronal(nullptr)
    , renderer_3d(nullptr)
{
    ui->setupUi(this);
    connect(ui->btn_open, &QPushButton::clicked, this, &Widget::onOpenDicom);
    
    // 获取 UI 中的 QVTKOpenGLNativeWidget
    view_axial = ui->view_axial;
    view_sagittal = ui->view_sagittal;
    view_coronal = ui->view_coronal;
    view_3d = ui->view_3d;
    
    // 为轴向视图创建渲染窗口和渲染器（红色背景）
    renderWindow_axial = vtkGenericOpenGLRenderWindow::New();
    view_axial->setRenderWindow(renderWindow_axial);
    renderer_axial = vtkRenderer::New();
    renderer_axial->SetBackground(1.0, 0.0, 0.0); // 红色背景
    renderWindow_axial->AddRenderer(renderer_axial);
    
    // 为矢状视图创建渲染窗口和渲染器（绿色背景）
    renderWindow_sagittal = vtkGenericOpenGLRenderWindow::New();
    view_sagittal->setRenderWindow(renderWindow_sagittal);
    renderer_sagittal = vtkRenderer::New();
    renderer_sagittal->SetBackground(0.0, 1.0, 0.0); // 绿色背景
    renderWindow_sagittal->AddRenderer(renderer_sagittal);
    
    // 为冠状视图创建渲染窗口和渲染器（蓝色背景）
    renderWindow_coronal = vtkGenericOpenGLRenderWindow::New();
    view_coronal->setRenderWindow(renderWindow_coronal);
    renderer_coronal = vtkRenderer::New();
    renderer_coronal->SetBackground(0.0, 0.0, 1.0); // 蓝色背景
    renderWindow_coronal->AddRenderer(renderer_coronal);
    
    // 为 3D 视图创建渲染窗口和渲染器（黑色背景）
    renderWindow_3d = vtkGenericOpenGLRenderWindow::New();
    view_3d->setRenderWindow(renderWindow_3d);
    renderer_3d = vtkRenderer::New();
    renderer_3d->SetBackground(0.0, 0.0, 0.0); // 黑色背景
    renderWindow_3d->AddRenderer(renderer_3d);
}

Widget::~Widget()
{
    // 释放 VTK 对象
    if (renderer_axial) {
        renderer_axial->Delete();
    }
    if (renderer_sagittal) {
        renderer_sagittal->Delete();
    }
    if (renderer_coronal) {
        renderer_coronal->Delete();
    }
    if (renderer_3d) {
        renderer_3d->Delete();
    }
    
    if (renderWindow_axial) {
        renderWindow_axial->Delete();
    }
    if (renderWindow_sagittal) {
        renderWindow_sagittal->Delete();
    }
    if (renderWindow_coronal) {
        renderWindow_coronal->Delete();
    }
    if (renderWindow_3d) {
        renderWindow_3d->Delete();
    }
    
    delete ui;
}

void Widget::onOpenDicom()
{
    const QString dirPath =
        QFileDialog::getExistingDirectory(this, QString::fromUtf8("选择 DICOM 目录"));
    if (dirPath.isEmpty()) {
        return;
    }

    using ReaderType = itk::ImageSeriesReader<ImageType>;
    auto reader = ReaderType::New();
    auto gdcmIO = itk::GDCMImageIO::New();
    auto fileNames = itk::GDCMSeriesFileNames::New();

    fileNames->SetUseSeriesDetails(true);
    fileNames->AddSeriesRestriction("0008|0021");
    fileNames->SetDirectory(dirPath.toStdString());

    const auto &seriesUIDs = fileNames->GetSeriesUIDs();
    if (seriesUIDs.empty()) {
        QMessageBox::warning(this, QString::fromUtf8("提示"), QString::fromUtf8("未找到 DICOM 序列。"));
        return;
    }

    reader->SetImageIO(gdcmIO);
    reader->SetFileNames(fileNames->GetFileNames(seriesUIDs.front()));

    try {
        reader->Update();
    } catch (const itk::ExceptionObject &ex) {
        QMessageBox::critical(this, QString::fromUtf8("错误"),
                              QString::fromUtf8("读取失败：%1").arg(QString::fromLocal8Bit(ex.what())));
        return;
    }

    vtkSmartPointer<vtkImageData> vtkImage = ItkToVtkImage(reader->GetOutput());
    if (!vtkImage) {
        QMessageBox::warning(this, QString::fromUtf8("提示"), QString::fromUtf8("转换图像失败。"));
        return;
    }

    // 健壮性处理：如果已经存在 viewer，先重置输入数据
    if (m_viewerAxial) {
        m_viewerAxial->SetInputData(nullptr);
    }
    if (m_viewerSagittal) {
        m_viewerSagittal->SetInputData(nullptr);
    }
    if (m_viewerCoronal) {
        m_viewerCoronal->SetInputData(nullptr);
    }

    // 初始化轴向视图
    if (!m_viewerAxial) {
        m_viewerAxial = vtkSmartPointer<vtkResliceImageViewer>::New();
        auto axialWindow = view_axial->renderWindow();
        m_viewerAxial->SetRenderWindow(axialWindow);
        m_viewerAxial->SetupInteractor(axialWindow->GetInteractor());
        m_viewerAxial->SetSliceOrientationToXY();
    }

    // 初始化矢状位视图
    if (!m_viewerSagittal) {
        m_viewerSagittal = vtkSmartPointer<vtkResliceImageViewer>::New();
        auto sagittalWindow = view_sagittal->renderWindow();
        m_viewerSagittal->SetRenderWindow(sagittalWindow);
        m_viewerSagittal->SetupInteractor(sagittalWindow->GetInteractor());
        m_viewerSagittal->SetSliceOrientationToYZ();
        // 设置背景颜色为绿色（与初始化时一致）
        if (renderer_sagittal) {
            renderer_sagittal->SetBackground(0.0, 1.0, 0.0);
        }
    }

    // 初始化冠状位视图
    if (!m_viewerCoronal) {
        m_viewerCoronal = vtkSmartPointer<vtkResliceImageViewer>::New();
        auto coronalWindow = view_coronal->renderWindow();
        m_viewerCoronal->SetRenderWindow(coronalWindow);
        m_viewerCoronal->SetupInteractor(coronalWindow->GetInteractor());
        m_viewerCoronal->SetSliceOrientationToXZ();
        // 设置背景颜色为蓝色（与初始化时一致）
        if (renderer_coronal) {
            renderer_coronal->SetBackground(0.0, 0.0, 1.0);
        }
    }

    // 使用同一个 vtkImageData 对象设置所有视图的输入数据
    m_viewerAxial->SetInputData(vtkImage);
    m_viewerSagittal->SetInputData(vtkImage);
    m_viewerCoronal->SetInputData(vtkImage);

    // 初始化滑动条范围并连接信号
    // 获取 QSlider 指针（使用 qobject_cast 确保类型安全）
    QSlider *sliderAxial = qobject_cast<QSlider*>(ui->slider_axial);
    QSlider *sliderSagittal = qobject_cast<QSlider*>(ui->slider_sagittal);
    QSlider *sliderCoronal = qobject_cast<QSlider*>(ui->slider_coronal);

    if (!sliderAxial || !sliderSagittal || !sliderCoronal) {
        QMessageBox::warning(this, QString::fromUtf8("错误"), QString::fromUtf8("滑动条控件未找到或类型不正确。"));
        return;
    }

    // 断开旧连接（如果存在）以防止重复绑定
    disconnect(sliderAxial, &QSlider::valueChanged, this, nullptr);
    disconnect(sliderSagittal, &QSlider::valueChanged, this, nullptr);
    disconnect(sliderCoronal, &QSlider::valueChanged, this, nullptr);

    // 设置轴向滑动条
    int axialMin = m_viewerAxial->GetSliceMin();
    int axialMax = m_viewerAxial->GetSliceMax();
    sliderAxial->setRange(axialMin, axialMax);
    // 设置滑动条默认值为中值
    int axialMid = (axialMin + axialMax) / 2;
    m_viewerAxial->SetSlice(axialMid);
    sliderAxial->setValue(axialMid);
    connect(sliderAxial, &QSlider::valueChanged, this, &Widget::onSliderAxialChanged, Qt::UniqueConnection);

    // 设置矢状位滑动条
    int sagittalMin = m_viewerSagittal->GetSliceMin();
    int sagittalMax = m_viewerSagittal->GetSliceMax();
    sliderSagittal->setRange(sagittalMin, sagittalMax);
    // 设置滑动条默认值为中值
    int sagittalMid = (sagittalMin + sagittalMax) / 2;
    m_viewerSagittal->SetSlice(sagittalMid);
    sliderSagittal->setValue(sagittalMid);
    connect(sliderSagittal, &QSlider::valueChanged, this, &Widget::onSliderSagittalChanged, Qt::UniqueConnection);

    // 设置冠状位滑动条
    int coronalMin = m_viewerCoronal->GetSliceMin();
    int coronalMax = m_viewerCoronal->GetSliceMax();
    sliderCoronal->setRange(coronalMin, coronalMax);
    // 设置滑动条默认值为中值
    int coronalMid = (coronalMin + coronalMax) / 2;
    m_viewerCoronal->SetSlice(coronalMid);
    sliderCoronal->setValue(coronalMid);
    connect(sliderCoronal, &QSlider::valueChanged, this, &Widget::onSliderCoronalChanged, Qt::UniqueConnection);

    // 调整视图窗口以适合主窗口（重置相机并适应窗口大小）
    // 使用 vtkResliceImageViewer 的 GetRenderer 方法获取渲染器并重置相机
    if (m_viewerAxial) {
        vtkRenderer *axialRenderer = m_viewerAxial->GetRenderer();
        if (axialRenderer) {
            axialRenderer->ResetCamera();
        }
    }
    if (m_viewerSagittal) {
        vtkRenderer *sagittalRenderer = m_viewerSagittal->GetRenderer();
        if (sagittalRenderer) {
            sagittalRenderer->ResetCamera();
        }
    }
    if (m_viewerCoronal) {
        vtkRenderer *coronalRenderer = m_viewerCoronal->GetRenderer();
        if (coronalRenderer) {
            coronalRenderer->ResetCamera();
        }
    }

    // 刷新所有窗口
    m_viewerAxial->Render();
    m_viewerSagittal->Render();
    m_viewerCoronal->Render();
}

vtkSmartPointer<vtkImageData> Widget::ItkToVtkImage(ImageType *image)
{
    if (!image) {
        return nullptr;
    }

    const auto region = image->GetLargestPossibleRegion();
    const auto size = region.GetSize();
    const auto spacing = image->GetSpacing();
    const auto origin = image->GetOrigin();

    auto vtkImage = vtkSmartPointer<vtkImageData>::New();
    vtkImage->SetDimensions(static_cast<int>(size[0]),
                            static_cast<int>(size[1]),
                            static_cast<int>(size[2]));
    vtkImage->SetSpacing(spacing[0], spacing[1], spacing[2]);
    vtkImage->SetOrigin(origin[0], origin[1], origin[2]);
    vtkImage->AllocateScalars(VTK_SHORT, 1);

    const size_t pixelCount = region.GetNumberOfPixels();
    std::memcpy(vtkImage->GetScalarPointer(),
                image->GetBufferPointer(),
                pixelCount * sizeof(PixelType));

    return vtkImage;
}

void Widget::onSliderAxialChanged(int value)
{
    if (m_viewerAxial) {
        m_viewerAxial->SetSlice(value);
        m_viewerAxial->Render();
    }
}

void Widget::onSliderSagittalChanged(int value)
{
    if (m_viewerSagittal) {
        m_viewerSagittal->SetSlice(value);
        m_viewerSagittal->Render();
    }
}

void Widget::onSliderCoronalChanged(int value)
{
    if (m_viewerCoronal) {
        m_viewerCoronal->SetSlice(value);
        m_viewerCoronal->Render();
    }
}
