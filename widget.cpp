#include "widget.h"
#include "./ui_widget.h"

#if defined(_MSC_VER) && (_MSC_VER >= 1600)
# pragma execution_character_set("utf-8")
#endif

#include <QFileDialog>
#include <QMessageBox>
#include <QSlider>
#include <QSignalBlocker>

#include <algorithm>
#include <cstring>
#include <cmath>

// VTK 模块初始化（必须在包含其他 VTK 头文件之前）
#include <vtkAutoInit.h>
VTK_MODULE_INIT(vtkRenderingOpenGL2);
VTK_MODULE_INIT(vtkInteractionStyle);
VTK_MODULE_INIT(vtkRenderingFreeType);

#include <QVTKOpenGLNativeWidget.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkRenderer.h>
#include <vtkCommand.h>
#include <vtkImagePlaneWidget.h>
#include <vtkOutlineFilter.h>
#include <vtkPolyDataMapper.h>
#include <vtkActor.h>
#include <vtkProperty.h>
#include <vtkCornerAnnotation.h>
#include <vtkTextProperty.h>
#include <vtkCellPicker.h>
#include <vtkInteractorStyleImage.h>
#include <vtkRenderWindowInteractor.h>

#include <itkImageSeriesReader.h>
#include <itkGDCMImageIO.h>
#include <itkGDCMSeriesFileNames.h>
#include <itkMetaDataObject.h>

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
    , m_axialObserverTag(0)
    , m_sagittalObserverTag(0)
    , m_coronalObserverTag(0)
    , m_axialClickTag(0)
    , m_sagittalClickTag(0)
    , m_coronalClickTag(0)
    , m_patientName("N/A")
    , m_patientID("N/A")
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

    // 读取 DICOM 元数据（患者姓名 / ID）
    m_patientName = "N/A";
    m_patientID   = "N/A";
    try {
        const auto &dictArray = *(reader->GetMetaDataDictionaryArray());
        if (!dictArray.empty() && dictArray[0]) {
            const auto &dict = *dictArray[0];
            m_patientName = GetDicomValue(dict, "0010|0010");
            m_patientID   = GetDicomValue(dict, "0010|0020");
        } else {
            // 回退到 GDCM ImageIO 的字典
            const auto &dict = gdcmIO->GetMetaDataDictionary();
            m_patientName = GetDicomValue(dict, "0010|0010");
            m_patientID   = GetDicomValue(dict, "0010|0020");
        }
    } catch (...) {
        // 保持默认值 "N/A"
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

    // 初始化 / 添加 2D 视图角标
    if (!m_annotAxial) {
        m_annotAxial = vtkSmartPointer<vtkCornerAnnotation>::New();
        m_annotAxial->GetTextProperty()->SetColor(1.0, 1.0, 0.0); // 黄色
        m_annotAxial->SetMaximumFontSize(14);
        m_viewerAxial->GetRenderer()->AddViewProp(m_annotAxial);
    }
    if (!m_annotSagittal) {
        m_annotSagittal = vtkSmartPointer<vtkCornerAnnotation>::New();
        m_annotSagittal->GetTextProperty()->SetColor(1.0, 1.0, 0.0);
        m_annotSagittal->SetMaximumFontSize(14);
        m_viewerSagittal->GetRenderer()->AddViewProp(m_annotSagittal);
    }
    if (!m_annotCoronal) {
        m_annotCoronal = vtkSmartPointer<vtkCornerAnnotation>::New();
        m_annotCoronal->GetTextProperty()->SetColor(1.0, 1.0, 0.0);
        m_annotCoronal->SetMaximumFontSize(14);
        m_viewerCoronal->GetRenderer()->AddViewProp(m_annotCoronal);
    }

    // 计算各个方向的中间切片索引
    const auto region = reader->GetOutput()->GetLargestPossibleRegion();
    const auto size = region.GetSize();
    int axialMidIndex     = static_cast<int>(size[2] / 2); // Z 轴
    int sagittalMidIndex  = static_cast<int>(size[0] / 2); // X 轴
    int coronalMidIndex   = static_cast<int>(size[1] / 2); // Y 轴

    // 初始化滑动条范围并连接信号
    // 获取 QSlider 指针（使用 qobject_cast 确保类型安全）
    QSlider *sliderAxial    = qobject_cast<QSlider*>(ui->slider_axial);
    QSlider *sliderSagittal = qobject_cast<QSlider*>(ui->slider_sagittal);
    QSlider *sliderCoronal  = qobject_cast<QSlider*>(ui->slider_coronal);
    QSlider *sliderWindow   = qobject_cast<QSlider*>(ui->slider_window);
    QSlider *sliderLevel    = qobject_cast<QSlider*>(ui->slider_level);

    if (!sliderAxial || !sliderSagittal || !sliderCoronal ||
        !sliderWindow || !sliderLevel) {
        QMessageBox::warning(this, QString::fromUtf8("错误"), QString::fromUtf8("滑动条控件未找到或类型不正确。"));
        return;
    }

    // 断开旧连接（如果存在）以防止重复绑定
    disconnect(sliderAxial,    &QSlider::valueChanged, this, nullptr);
    disconnect(sliderSagittal, &QSlider::valueChanged, this, nullptr);
    disconnect(sliderCoronal,  &QSlider::valueChanged, this, nullptr);
    disconnect(sliderWindow,   &QSlider::valueChanged, this, nullptr);
    disconnect(sliderLevel,    &QSlider::valueChanged, this, nullptr);

    // 设置轴向滑动条
    int axialMin = m_viewerAxial->GetSliceMin();
    int axialMax = m_viewerAxial->GetSliceMax();
    sliderAxial->setRange(axialMin, axialMax);
    // 设置滑动条默认值为中值（同时用于 3D 平面）
    int axialMid = std::clamp(axialMidIndex, axialMin, axialMax);
    m_viewerAxial->SetSlice(axialMid);
    sliderAxial->setValue(axialMid);
    connect(sliderAxial, &QSlider::valueChanged, this, &Widget::onSliderAxialChanged, Qt::UniqueConnection);

    // 设置矢状位滑动条
    int sagittalMin = m_viewerSagittal->GetSliceMin();
    int sagittalMax = m_viewerSagittal->GetSliceMax();
    sliderSagittal->setRange(sagittalMin, sagittalMax);
    // 设置滑动条默认值为中值（同时用于 3D 平面）
    int sagittalMid = std::clamp(sagittalMidIndex, sagittalMin, sagittalMax);
    m_viewerSagittal->SetSlice(sagittalMid);
    sliderSagittal->setValue(sagittalMid);
    connect(sliderSagittal, &QSlider::valueChanged, this, &Widget::onSliderSagittalChanged, Qt::UniqueConnection);

    // 设置冠状位滑动条
    int coronalMin = m_viewerCoronal->GetSliceMin();
    int coronalMax = m_viewerCoronal->GetSliceMax();
    sliderCoronal->setRange(coronalMin, coronalMax);
    // 设置滑动条默认值为中值（同时用于 3D 平面）
    int coronalMid = std::clamp(coronalMidIndex, coronalMin, coronalMax);
    m_viewerCoronal->SetSlice(coronalMid);
    sliderCoronal->setValue(coronalMid);

    // 设置窗宽 / 窗位滑动条的范围和默认值
    sliderWindow->setRange(1, 3000);
    sliderWindow->setValue(2000);
    sliderLevel->setRange(-1000, 1000);
    sliderLevel->setValue(40);

    // 初始化 3D 视图中的三个切片平面
    auto interactor3D = view_3d->renderWindow()->GetInteractor();
    if (interactor3D && renderer_3d) {
        if (!m_planeAxial) {
            m_planeAxial = vtkSmartPointer<vtkImagePlaneWidget>::New();
        }
        if (!m_planeSagittal) {
            m_planeSagittal = vtkSmartPointer<vtkImagePlaneWidget>::New();
        }
        if (!m_planeCoronal) {
            m_planeCoronal = vtkSmartPointer<vtkImagePlaneWidget>::New();
        }

        double window = m_viewerAxial->GetColorWindow();
        double level  = m_viewerAxial->GetColorLevel();

        // 轴向平面（Z 轴）
        m_planeAxial->SetInteractor(interactor3D);
        m_planeAxial->SetInputData(vtkImage);
        m_planeAxial->SetPlaneOrientationToZAxes();
        m_planeAxial->SetSliceIndex(axialMid);
        m_planeAxial->SetWindowLevel(window, level);
        m_planeAxial->DisplayTextOff();
        m_planeAxial->SetMarginSizeX(0);
        m_planeAxial->SetMarginSizeY(0);
        m_planeAxial->GetPlaneProperty()->SetColor(1.0, 0.0, 0.0); // 红色
        m_planeAxial->On();
        m_planeAxial->InteractionOff(); // 只显示，不允许拖动改变切片

        // 矢状位平面（X 轴）
        m_planeSagittal->SetInteractor(interactor3D);
        m_planeSagittal->SetInputData(vtkImage);
        m_planeSagittal->SetPlaneOrientationToXAxes();
        m_planeSagittal->SetSliceIndex(sagittalMid);
        m_planeSagittal->SetWindowLevel(window, level);
        m_planeSagittal->DisplayTextOff();
        m_planeSagittal->SetMarginSizeX(0);
        m_planeSagittal->SetMarginSizeY(0);
        m_planeSagittal->GetPlaneProperty()->SetColor(0.0, 1.0, 0.0); // 绿色
        m_planeSagittal->On();
        m_planeSagittal->InteractionOff();

        // 冠状位平面（Y 轴）
        m_planeCoronal->SetInteractor(interactor3D);
        m_planeCoronal->SetInputData(vtkImage);
        m_planeCoronal->SetPlaneOrientationToYAxes();
        m_planeCoronal->SetSliceIndex(coronalMid);
        m_planeCoronal->SetWindowLevel(window, level);
        m_planeCoronal->DisplayTextOff();
        m_planeCoronal->SetMarginSizeX(0);
        m_planeCoronal->SetMarginSizeY(0);
        m_planeCoronal->GetPlaneProperty()->SetColor(0.0, 0.0, 1.0); // 蓝色
        m_planeCoronal->On();
        m_planeCoronal->InteractionOff();

        // 创建 / 更新 立方体外框
        auto outlineFilter = vtkSmartPointer<vtkOutlineFilter>::New();
        outlineFilter->SetInputData(vtkImage);
        outlineFilter->Update();

        auto outlineMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        outlineMapper->SetInputConnection(outlineFilter->GetOutputPort());

        auto outlineActor = vtkSmartPointer<vtkActor>::New();
        outlineActor->SetMapper(outlineMapper);
        outlineActor->GetProperty()->SetColor(1.0, 1.0, 1.0); // 白色外框

        // 如果已有外框，先移除
        if (m_outlineActor) {
            renderer_3d->RemoveActor(m_outlineActor);
        }
        m_outlineActor = outlineActor;
        renderer_3d->AddActor(m_outlineActor);

        // 重置相机以包含所有对象
        renderer_3d->ResetCamera();
        renderWindow_3d->Render();
    }
    connect(sliderCoronal, &QSlider::valueChanged, this, &Widget::onSliderCoronalChanged, Qt::UniqueConnection);
    connect(sliderWindow,  &QSlider::valueChanged, this, &Widget::onWindowLevelChanged, Qt::UniqueConnection);
    connect(sliderLevel,   &QSlider::valueChanged, this, &Widget::onWindowLevelChanged, Qt::UniqueConnection);

    // 应用一次默认窗宽 / 窗位
    onWindowLevelChanged();
    UpdateAnnotations();

    // 注册 VTK -> Qt 的交互回调，实现滚轮和滑动条同步
    registerSliceObserver(m_viewerAxial, m_axialSliceCallback, m_axialObserverTag);
    registerSliceObserver(m_viewerSagittal, m_sagittalSliceCallback, m_sagittalObserverTag);
    registerSliceObserver(m_viewerCoronal, m_coronalSliceCallback, m_coronalObserverTag);

    // 注册鼠标点击回调，实现三视图光标联动
    auto registerClick = [&](vtkResliceImageViewer *viewer,
                             vtkSmartPointer<vtkCallbackCommand> &callback,
                             unsigned long &tag) {
        if (!viewer) return;
        auto *style = vtkInteractorStyleImage::SafeDownCast(viewer->GetInteractorStyle());
        if (!style) return;

        if (!callback) {
            callback = vtkSmartPointer<vtkCallbackCommand>::New();
            callback->SetCallback(Widget::OnClickCallback);
        }
        callback->SetClientData(this);

        if (tag != 0) {
            style->RemoveObserver(tag);
        }
        tag = style->AddObserver(vtkCommand::LeftButtonPressEvent, callback);
    };
    registerClick(m_viewerAxial,    m_axialClickCallback,    m_axialClickTag);
    registerClick(m_viewerSagittal, m_sagittalClickCallback, m_sagittalClickTag);
    registerClick(m_viewerCoronal,  m_coronalClickCallback,  m_coronalClickTag);

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

std::string Widget::GetDicomValue(const itk::MetaDataDictionary &dict,
                                  const std::string &tagKey) const
{
    using MetaStringType = itk::MetaDataObject<std::string>;
    auto it = dict.Find(tagKey);
    if (it == dict.End()) {
        return "N/A";
    }
    const auto *metaObj = dynamic_cast<const MetaStringType*>(it->second.GetPointer());
    if (!metaObj) {
        return "N/A";
    }
    const std::string &value = metaObj->GetMetaDataObjectValue();
    if (value.empty()) {
        return "N/A";
    }
    return value;
}

void Widget::onSliderAxialChanged(int value)
{
    if (m_viewerAxial) {
        m_viewerAxial->SetSlice(value);
        m_viewerAxial->Render();
    }
    if (m_planeAxial) {
        m_planeAxial->SetSliceIndex(value);
    }
    if (renderWindow_3d) {
        renderWindow_3d->Render();
    }
    UpdateAnnotations();
}

void Widget::onSliderSagittalChanged(int value)
{
    if (m_viewerSagittal) {
        m_viewerSagittal->SetSlice(value);
        m_viewerSagittal->Render();
    }
    if (m_planeSagittal) {
        m_planeSagittal->SetSliceIndex(value);
    }
    if (renderWindow_3d) {
        renderWindow_3d->Render();
    }
    UpdateAnnotations();
}

void Widget::onSliderCoronalChanged(int value)
{
    if (m_viewerCoronal) {
        m_viewerCoronal->SetSlice(value);
        m_viewerCoronal->Render();
    }
    if (m_planeCoronal) {
        m_planeCoronal->SetSliceIndex(value);
    }
    if (renderWindow_3d) {
        renderWindow_3d->Render();
    }
    UpdateAnnotations();
}

void Widget::onWindowLevelChanged()
{
    QSlider *sliderWindow = qobject_cast<QSlider*>(ui->slider_window);
    QSlider *sliderLevel  = qobject_cast<QSlider*>(ui->slider_level);
    if (!sliderWindow || !sliderLevel) {
        return;
    }

    const double w = static_cast<double>(sliderWindow->value());
    const double l = static_cast<double>(sliderLevel->value());

    // 更新 2D 视图窗宽 / 窗位
    auto applyWL2D = [w, l](vtkResliceImageViewer *viewer) {
        if (!viewer) return;
        viewer->SetColorWindow(w);
        viewer->SetColorLevel(l);
        viewer->Render();
    };
    applyWL2D(m_viewerAxial);
    applyWL2D(m_viewerSagittal);
    applyWL2D(m_viewerCoronal);

    // 更新 3D 平面窗宽 / 窗位
    auto applyWL3D = [w, l](vtkImagePlaneWidget *plane) {
        if (!plane) return;
        plane->SetWindowLevel(w, l);
    };
    applyWL3D(m_planeAxial);
    applyWL3D(m_planeSagittal);
    applyWL3D(m_planeCoronal);

    if (renderWindow_3d) {
        renderWindow_3d->Render();
    }
    UpdateAnnotations();
}

void Widget::registerSliceObserver(vtkResliceImageViewer *viewer,
                                   vtkSmartPointer<vtkCallbackCommand> &callback,
                                   unsigned long &observerTag)
{
    if (!viewer) {
        return;
    }

    if (!callback) {
        callback = vtkSmartPointer<vtkCallbackCommand>::New();
        callback->SetCallback(Widget::SliceChangedCallback);
    }

    callback->SetClientData(this);

    if (observerTag != 0) {
        viewer->RemoveObserver(observerTag);
    }

    observerTag = viewer->AddObserver(vtkResliceImageViewer::SliceChangedEvent, callback);
}

void Widget::handleSliceInteraction(vtkObject *caller)
{
    if (!caller) {
        return;
    }

    auto *viewerCaller = vtkResliceImageViewer::SafeDownCast(caller);
    if (!viewerCaller) {
        return;
    }

    auto *sliderAxial = qobject_cast<QSlider*>(ui->slider_axial);
    auto *sliderSagittal = qobject_cast<QSlider*>(ui->slider_sagittal);
    auto *sliderCoronal = qobject_cast<QSlider*>(ui->slider_coronal);

    if (m_viewerAxial && m_viewerAxial == viewerCaller) {
        syncSliderWithViewer(sliderAxial, m_viewerAxial);
    } else if (m_viewerSagittal && m_viewerSagittal == viewerCaller) {
        syncSliderWithViewer(sliderSagittal, m_viewerSagittal);
    } else if (m_viewerCoronal && m_viewerCoronal == viewerCaller) {
        syncSliderWithViewer(sliderCoronal, m_viewerCoronal);
    }
    UpdateAnnotations();
}

void Widget::syncSliderWithViewer(QSlider *slider, vtkResliceImageViewer *viewer)
{
    if (!slider || !viewer) {
        return;
    }

    QSignalBlocker blocker(slider);
    slider->setValue(viewer->GetSlice());
}

void Widget::SliceChangedCallback(vtkObject* caller,
                                  unsigned long /*eventId*/,
                                  void* clientData,
                                  void* /*callData*/)
{
    auto *self = static_cast<Widget*>(clientData);
    if (!self) {
        return;
    }
    self->handleSliceInteraction(caller);
}

void Widget::UpdateAnnotations()
{
    if (!m_viewerAxial || !m_viewerSagittal || !m_viewerCoronal) {
        return;
    }

    auto updateForViewer = [this](vtkResliceImageViewer *viewer,
                                  vtkCornerAnnotation *annot,
                                  const char *orientationLabel) {
        if (!viewer || !annot) return;

        int sliceMin = viewer->GetSliceMin();
        int sliceMax = viewer->GetSliceMax();
        int totalSlices = sliceMax - sliceMin + 1;
        if (totalSlices < 1) totalSlices = 1;

        int slice = viewer->GetSlice() - sliceMin + 1; // 1-based
        if (slice < 1) slice = 1;
        if (slice > totalSlices) slice = totalSlices;

        // 左上角：患者信息 + 方向
        std::string topLeft = "Name: " + m_patientName +
                              "\nID: " + m_patientID +
                              "\nView: " + (orientationLabel ? orientationLabel : "");
        annot->SetText(0, topLeft.c_str());

        // 左下角：切片信息
        std::string bottomLeft = "Slice: " + std::to_string(slice) +
                                 " / " + std::to_string(totalSlices);
        annot->SetText(1, bottomLeft.c_str());

        // 右下角：窗宽 / 窗位
        double w = viewer->GetColorWindow();
        double l = viewer->GetColorLevel();
        std::string bottomRight = "W: " + std::to_string(static_cast<int>(w)) +
                                  "  L: " + std::to_string(static_cast<int>(l));
        annot->SetText(2, bottomRight.c_str());
    };

    updateForViewer(m_viewerAxial,    m_annotAxial,    "Axial");
    updateForViewer(m_viewerSagittal, m_annotSagittal, "Sagittal");
    updateForViewer(m_viewerCoronal,  m_annotCoronal,  "Coronal");

    m_viewerAxial->Render();
    m_viewerSagittal->Render();
    m_viewerCoronal->Render();
}

void Widget::OnClickCallback(vtkObject* caller,
                             unsigned long /*eventId*/,
                             void* clientData,
                             void* /*callData*/)
{
    auto *self = static_cast<Widget*>(clientData);
    if (!self) {
        return;
    }

    auto *style = vtkInteractorStyleImage::SafeDownCast(caller);
    if (!style) {
        return;
    }

    vtkRenderWindowInteractor *interactor = style->GetInteractor();
    if (!interactor) {
        return;
    }

    vtkResliceImageViewer *sourceViewer = nullptr;
    if (self->m_viewerAxial && self->m_viewerAxial->GetInteractorStyle() == style) {
        sourceViewer = self->m_viewerAxial;
    } else if (self->m_viewerSagittal && self->m_viewerSagittal->GetInteractorStyle() == style) {
        sourceViewer = self->m_viewerSagittal;
    } else if (self->m_viewerCoronal && self->m_viewerCoronal->GetInteractorStyle() == style) {
        sourceViewer = self->m_viewerCoronal;
    }

    if (!sourceViewer) {
        return;
    }

    vtkRenderer *renderer = sourceViewer->GetRenderer();
    if (!renderer) {
        return;
    }

    self->HandleViewClick(sourceViewer, interactor, renderer);
}

void Widget::HandleViewClick(vtkResliceImageViewer *viewer,
                             vtkRenderWindowInteractor *interactor,
                             vtkRenderer *renderer)
{
    if (!viewer || !interactor || !renderer) {
        return;
    }

    vtkSmartPointer<vtkCellPicker> picker = vtkSmartPointer<vtkCellPicker>::New();
    picker->SetTolerance(0.005); // 小的世界坐标容差

    int pos[2];
    interactor->GetEventPosition(pos);

    if (!picker->Pick(pos[0], pos[1], 0, renderer)) {
        return;
    }

    double pickPos[3];
    picker->GetPickPosition(pickPos);

    vtkImageData *imageData = viewer->GetInput();
    if (!imageData) {
        return;
    }

    double origin[3];
    double spacing[3];
    imageData->GetOrigin(origin);
    imageData->GetSpacing(spacing);

    auto worldToIndex = [&](double worldCoord, double org, double spc) -> int {
        return static_cast<int>(std::round((worldCoord - org) / spc));
    };

    int idxX = worldToIndex(pickPos[0], origin[0], spacing[0]);
    int idxY = worldToIndex(pickPos[1], origin[1], spacing[1]);
    int idxZ = worldToIndex(pickPos[2], origin[2], spacing[2]);

    auto clampIndex = [](int idx, int minVal, int maxVal) -> int {
        if (idx < minVal) return minVal;
        if (idx > maxVal) return maxVal;
        return idx;
    };

    int axialMin = m_viewerAxial ? m_viewerAxial->GetSliceMin() : 0;
    int axialMax = m_viewerAxial ? m_viewerAxial->GetSliceMax() : 0;
    idxZ = clampIndex(idxZ, axialMin, axialMax);

    int sagittalMin = m_viewerSagittal ? m_viewerSagittal->GetSliceMin() : 0;
    int sagittalMax = m_viewerSagittal ? m_viewerSagittal->GetSliceMax() : 0;
    idxX = clampIndex(idxX, sagittalMin, sagittalMax);

    int coronalMin = m_viewerCoronal ? m_viewerCoronal->GetSliceMin() : 0;
    int coronalMax = m_viewerCoronal ? m_viewerCoronal->GetSliceMax() : 0;
    idxY = clampIndex(idxY, coronalMin, coronalMax);

    if (auto slider = qobject_cast<QSlider*>(ui->slider_axial)) {
        slider->setValue(idxZ);
    }
    if (auto slider = qobject_cast<QSlider*>(ui->slider_sagittal)) {
        slider->setValue(idxX);
    }
    if (auto slider = qobject_cast<QSlider*>(ui->slider_coronal)) {
        slider->setValue(idxY);
    }
}
