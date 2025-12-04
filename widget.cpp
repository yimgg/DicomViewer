#include "widget.h"
#include "./ui_widget.h"

#if defined(_MSC_VER) && (_MSC_VER >= 1600)
# pragma execution_character_set("utf-8")
#endif

#include <QFileDialog>
#include <QMessageBox>
#include <QSlider>
#include <QSignalBlocker>
#include <QTextCodec>
#include <QFile>
#include <QStringList>

#include <algorithm>
#include <cstring>
#include <cmath>

// VTK module init
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
#include <vtkDistanceWidget.h>
#include <vtkDistanceRepresentation2D.h>
#include <vtkProperty2D.h>
#include <vtkImageReslice.h>
#include <vtkImageMapToColors.h>
#include <vtkImageActor.h>
#include <vtkLookupTable.h>
#include <vtkMatrix4x4.h>
#include <vtkImageProperty.h>
#include <vtkExtractVOI.h>
#include <vtkImagePermute.h>

#include <itkImageSeriesReader.h>
#include <itkImageFileReader.h>
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
    connect(ui->btn_measure, &QPushButton::toggled, this, &Widget::onMeasureToggled);
    connect(ui->btn_load_mask, &QPushButton::clicked, this, &Widget::onLoadMask);
    
    view_axial = ui->view_axial;
    view_sagittal = ui->view_sagittal;
    view_coronal = ui->view_coronal;
    view_3d = ui->view_3d;
    
    renderWindow_axial = vtkGenericOpenGLRenderWindow::New();
    view_axial->setRenderWindow(renderWindow_axial);
    renderer_axial = vtkRenderer::New();
    renderer_axial->SetBackground(1.0, 0.0, 0.0);
    renderWindow_axial->AddRenderer(renderer_axial);
    
    renderWindow_sagittal = vtkGenericOpenGLRenderWindow::New();
    view_sagittal->setRenderWindow(renderWindow_sagittal);
    renderer_sagittal = vtkRenderer::New();
    renderer_sagittal->SetBackground(0.0, 1.0, 0.0);
    renderWindow_sagittal->AddRenderer(renderer_sagittal);
    
    renderWindow_coronal = vtkGenericOpenGLRenderWindow::New();
    view_coronal->setRenderWindow(renderWindow_coronal);
    renderer_coronal = vtkRenderer::New();
    renderer_coronal->SetBackground(0.0, 0.0, 1.0);
    renderWindow_coronal->AddRenderer(renderer_coronal);
    
    renderWindow_3d = vtkGenericOpenGLRenderWindow::New();
    view_3d->setRenderWindow(renderWindow_3d);
    renderer_3d = vtkRenderer::New();
    renderer_3d->SetBackground(0.0, 0.0, 0.0);
    renderWindow_3d->AddRenderer(renderer_3d);
}

Widget::~Widget()
{
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
    const QString dirPath = QFileDialog::getExistingDirectory(this, QStringLiteral("Select DICOM Directory"));
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
        QMessageBox::warning(this, QStringLiteral("Warning"), QStringLiteral("No DICOM series found."));
        return;
    }

    reader->SetImageIO(gdcmIO);
    reader->SetFileNames(fileNames->GetFileNames(seriesUIDs.front()));

    try {
        reader->Update();
    } catch (const itk::ExceptionObject &ex) {
        QMessageBox::critical(this, QStringLiteral("Error"), 
                              QStringLiteral("Read failed: %1").arg(QString::fromLocal8Bit(ex.what())));
        return;
    }

    m_patientName = "N/A";
    m_patientID   = "N/A";
    try {
        const auto &dictArray = *(reader->GetMetaDataDictionaryArray());
        if (!dictArray.empty() && dictArray[0]) {
            const auto &dict = *dictArray[0];
            m_patientName = GetDicomValue(dict, "0010|0010");
            m_patientID   = GetDicomValue(dict, "0010|0020");
        } else {
            const auto &dict = gdcmIO->GetMetaDataDictionary();
            m_patientName = GetDicomValue(dict, "0010|0010");
            m_patientID   = GetDicomValue(dict, "0010|0020");
        }
    } catch (...) {
    }

    vtkSmartPointer<vtkImageData> vtkImage = ItkToVtkImage(reader->GetOutput());
    if (!vtkImage) {
        QMessageBox::warning(this, QStringLiteral("Warning"), QStringLiteral("Image conversion failed."));
        return;
    }

    if (m_distWidgetAxial) {
        m_distWidgetAxial->Off();
        m_distWidgetAxial->SetInteractor(nullptr);
    }
    if (m_distWidgetSagittal) {
        m_distWidgetSagittal->Off();
        m_distWidgetSagittal->SetInteractor(nullptr);
    }
    if (m_distWidgetCoronal) {
        m_distWidgetCoronal->Off();
        m_distWidgetCoronal->SetInteractor(nullptr);
    }

    if (m_maskAxial.actor && m_viewerAxial && m_viewerAxial->GetRenderer()) {
        m_viewerAxial->GetRenderer()->RemoveActor(m_maskAxial.actor);
    }
    if (m_maskSagittal.actor && m_viewerSagittal && m_viewerSagittal->GetRenderer()) {
        m_viewerSagittal->GetRenderer()->RemoveActor(m_maskSagittal.actor);
    }
    if (m_maskCoronal.actor && m_viewerCoronal && m_viewerCoronal->GetRenderer()) {
        m_viewerCoronal->GetRenderer()->RemoveActor(m_maskCoronal.actor);
    }
    m_maskAxial = MaskPipeline();
    m_maskSagittal = MaskPipeline();
    m_maskCoronal = MaskPipeline();
    m_maskData = nullptr;

    if (m_viewerAxial) {
        m_viewerAxial->SetInputData(nullptr);
    }
    if (m_viewerSagittal) {
        m_viewerSagittal->SetInputData(nullptr);
    }
    if (m_viewerCoronal) {
        m_viewerCoronal->SetInputData(nullptr);
    }

    if (!m_viewerAxial) {
        m_viewerAxial = vtkSmartPointer<vtkResliceImageViewer>::New();
        auto axialWindow = view_axial->renderWindow();
        m_viewerAxial->SetRenderWindow(axialWindow);
        m_viewerAxial->SetupInteractor(axialWindow->GetInteractor());
        m_viewerAxial->SetSliceOrientationToXY();
    }

    if (!m_viewerSagittal) {
        m_viewerSagittal = vtkSmartPointer<vtkResliceImageViewer>::New();
        auto sagittalWindow = view_sagittal->renderWindow();
        m_viewerSagittal->SetRenderWindow(sagittalWindow);
        m_viewerSagittal->SetupInteractor(sagittalWindow->GetInteractor());
        m_viewerSagittal->SetSliceOrientationToYZ();
        if (renderer_sagittal) {
            renderer_sagittal->SetBackground(0.0, 1.0, 0.0);
        }
    }

    if (!m_viewerCoronal) {
        m_viewerCoronal = vtkSmartPointer<vtkResliceImageViewer>::New();
        auto coronalWindow = view_coronal->renderWindow();
        m_viewerCoronal->SetRenderWindow(coronalWindow);
        m_viewerCoronal->SetupInteractor(coronalWindow->GetInteractor());
        m_viewerCoronal->SetSliceOrientationToXZ();
        if (renderer_coronal) {
            renderer_coronal->SetBackground(0.0, 0.0, 1.0);
        }
    }

    m_viewerAxial->SetInputData(vtkImage);
    m_viewerSagittal->SetInputData(vtkImage);
    m_viewerCoronal->SetInputData(vtkImage);

    QString chineseFontPath;
    QStringList fontPaths;
    fontPaths << "C:/Windows/Fonts/msyh.ttf";
    fontPaths << "C:/Windows/Fonts/simhei.ttf";
    fontPaths << "C:/Windows/Fonts/simsun.ttc";
    fontPaths << "C:/Windows/Fonts/msyhbd.ttf";
    
    for (int i = 0; i < fontPaths.size(); ++i) {
        if (QFile::exists(fontPaths[i])) {
            chineseFontPath = fontPaths[i];
            break;
        }
    }
    
    if (!m_annotAxial) {
        m_annotAxial = vtkSmartPointer<vtkCornerAnnotation>::New();
        vtkTextProperty* textProp = m_annotAxial->GetTextProperty();
        textProp->SetColor(1.0, 1.0, 0.0);
        if (!chineseFontPath.isEmpty()) {
            textProp->SetFontFamily(VTK_FONT_FILE);
            textProp->SetFontFile(chineseFontPath.toStdString().c_str());
        } else {
            textProp->SetFontFamilyToTimes();
        }
        m_annotAxial->SetMaximumFontSize(14);
        m_viewerAxial->GetRenderer()->AddViewProp(m_annotAxial);
    }
    if (!m_annotSagittal) {
        m_annotSagittal = vtkSmartPointer<vtkCornerAnnotation>::New();
        vtkTextProperty* textProp = m_annotSagittal->GetTextProperty();
        textProp->SetColor(1.0, 1.0, 0.0);
        if (!chineseFontPath.isEmpty()) {
            textProp->SetFontFamily(VTK_FONT_FILE);
            textProp->SetFontFile(chineseFontPath.toStdString().c_str());
        } else {
            textProp->SetFontFamilyToTimes();
        }
        m_annotSagittal->SetMaximumFontSize(14);
        m_viewerSagittal->GetRenderer()->AddViewProp(m_annotSagittal);
    }
    if (!m_annotCoronal) {
        m_annotCoronal = vtkSmartPointer<vtkCornerAnnotation>::New();
        vtkTextProperty* textProp = m_annotCoronal->GetTextProperty();
        textProp->SetColor(1.0, 1.0, 0.0);
        if (!chineseFontPath.isEmpty()) {
            textProp->SetFontFamily(VTK_FONT_FILE);
            textProp->SetFontFile(chineseFontPath.toStdString().c_str());
        } else {
            textProp->SetFontFamilyToTimes();
        }
        m_annotCoronal->SetMaximumFontSize(14);
        m_viewerCoronal->GetRenderer()->AddViewProp(m_annotCoronal);
    }

    const auto region = reader->GetOutput()->GetLargestPossibleRegion();
    const auto size = region.GetSize();
    int axialMidIndex     = static_cast<int>(size[2] / 2);
    int sagittalMidIndex  = static_cast<int>(size[0] / 2);
    int coronalMidIndex   = static_cast<int>(size[1] / 2);

    QSlider *sliderAxial    = qobject_cast<QSlider*>(ui->slider_axial);
    QSlider *sliderSagittal = qobject_cast<QSlider*>(ui->slider_sagittal);
    QSlider *sliderCoronal  = qobject_cast<QSlider*>(ui->slider_coronal);
    QSlider *sliderWindow   = qobject_cast<QSlider*>(ui->slider_window);
    QSlider *sliderLevel    = qobject_cast<QSlider*>(ui->slider_level);

    if (!sliderAxial || !sliderSagittal || !sliderCoronal || !sliderWindow || !sliderLevel) {
        QMessageBox::warning(this, QStringLiteral("Error"), QStringLiteral("Slider controls not found."));
        return;
    }

    disconnect(sliderAxial,    &QSlider::valueChanged, this, nullptr);
    disconnect(sliderSagittal, &QSlider::valueChanged, this, nullptr);
    disconnect(sliderCoronal,  &QSlider::valueChanged, this, nullptr);
    disconnect(sliderWindow,   &QSlider::valueChanged, this, nullptr);
    disconnect(sliderLevel,    &QSlider::valueChanged, this, nullptr);

    int axialMin = m_viewerAxial->GetSliceMin();
    int axialMax = m_viewerAxial->GetSliceMax();
    sliderAxial->setRange(axialMin, axialMax);
    int axialMid = std::clamp(axialMidIndex, axialMin, axialMax);
    m_viewerAxial->SetSlice(axialMid);
    disconnect(sliderAxial, &QSlider::valueChanged, this, &Widget::onSliderAxialChanged);
    sliderAxial->setValue(axialMid);
    connect(sliderAxial, &QSlider::valueChanged, this, &Widget::onSliderAxialChanged, Qt::UniqueConnection);

    int sagittalMin = m_viewerSagittal->GetSliceMin();
    int sagittalMax = m_viewerSagittal->GetSliceMax();
    sliderSagittal->setRange(sagittalMin, sagittalMax);
    int sagittalMid = std::clamp(sagittalMidIndex, sagittalMin, sagittalMax);
    m_viewerSagittal->SetSlice(sagittalMid);
    disconnect(sliderSagittal, &QSlider::valueChanged, this, &Widget::onSliderSagittalChanged);
    sliderSagittal->setValue(sagittalMid);
    connect(sliderSagittal, &QSlider::valueChanged, this, &Widget::onSliderSagittalChanged, Qt::UniqueConnection);

    int coronalMin = m_viewerCoronal->GetSliceMin();
    int coronalMax = m_viewerCoronal->GetSliceMax();
    sliderCoronal->setRange(coronalMin, coronalMax);
    int coronalMid = std::clamp(coronalMidIndex, coronalMin, coronalMax);
    m_viewerCoronal->SetSlice(coronalMid);
    disconnect(sliderCoronal, &QSlider::valueChanged, this, &Widget::onSliderCoronalChanged);
    sliderCoronal->setValue(coronalMid);
    connect(sliderCoronal, &QSlider::valueChanged, this, &Widget::onSliderCoronalChanged, Qt::UniqueConnection);

    sliderWindow->setRange(1, 3000);
    sliderWindow->setValue(2000);
    sliderLevel->setRange(-1000, 1000);
    sliderLevel->setValue(40);

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

        m_planeAxial->SetInteractor(interactor3D);
        m_planeAxial->SetInputData(vtkImage);
        m_planeAxial->SetPlaneOrientationToZAxes();
        m_planeAxial->SetSliceIndex(axialMid);
        m_planeAxial->SetWindowLevel(window, level);
        m_planeAxial->DisplayTextOff();
        m_planeAxial->SetMarginSizeX(0);
        m_planeAxial->SetMarginSizeY(0);
        m_planeAxial->GetPlaneProperty()->SetColor(1.0, 0.0, 0.0);
        m_planeAxial->On();
        m_planeAxial->InteractionOff();

        m_planeSagittal->SetInteractor(interactor3D);
        m_planeSagittal->SetInputData(vtkImage);
        m_planeSagittal->SetPlaneOrientationToXAxes();
        m_planeSagittal->SetSliceIndex(sagittalMid);
        m_planeSagittal->SetWindowLevel(window, level);
        m_planeSagittal->DisplayTextOff();
        m_planeSagittal->SetMarginSizeX(0);
        m_planeSagittal->SetMarginSizeY(0);
        m_planeSagittal->GetPlaneProperty()->SetColor(0.0, 1.0, 0.0);
        m_planeSagittal->On();
        m_planeSagittal->InteractionOff();

        m_planeCoronal->SetInteractor(interactor3D);
        m_planeCoronal->SetInputData(vtkImage);
        m_planeCoronal->SetPlaneOrientationToYAxes();
        m_planeCoronal->SetSliceIndex(coronalMid);
        m_planeCoronal->SetWindowLevel(window, level);
        m_planeCoronal->DisplayTextOff();
        m_planeCoronal->SetMarginSizeX(0);
        m_planeCoronal->SetMarginSizeY(0);
        m_planeCoronal->GetPlaneProperty()->SetColor(0.0, 0.0, 1.0);
        m_planeCoronal->On();
        m_planeCoronal->InteractionOff();

        auto outlineFilter = vtkSmartPointer<vtkOutlineFilter>::New();
        outlineFilter->SetInputData(vtkImage);
        outlineFilter->Update();

        auto outlineMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        outlineMapper->SetInputConnection(outlineFilter->GetOutputPort());

        auto outlineActor = vtkSmartPointer<vtkActor>::New();
        outlineActor->SetMapper(outlineMapper);
        outlineActor->GetProperty()->SetColor(1.0, 1.0, 1.0);

        if (m_outlineActor) {
            renderer_3d->RemoveActor(m_outlineActor);
        }
        m_outlineActor = outlineActor;
        renderer_3d->AddActor(m_outlineActor);

        renderer_3d->ResetCamera();
        renderWindow_3d->Render();
    }
    connect(sliderCoronal, &QSlider::valueChanged, this, &Widget::onSliderCoronalChanged, Qt::UniqueConnection);
    connect(sliderWindow,  &QSlider::valueChanged, this, &Widget::onWindowLevelChanged, Qt::UniqueConnection);
    connect(sliderLevel,   &QSlider::valueChanged, this, &Widget::onWindowLevelChanged, Qt::UniqueConnection);

    onWindowLevelChanged();
    UpdateAnnotations();

    registerSliceObserver(m_viewerAxial, m_axialSliceCallback, m_axialObserverTag);
    registerSliceObserver(m_viewerSagittal, m_sagittalSliceCallback, m_sagittalObserverTag);
    registerSliceObserver(m_viewerCoronal, m_coronalSliceCallback, m_coronalObserverTag);

    if (m_viewerAxial) {
        auto *style = vtkInteractorStyleImage::SafeDownCast(m_viewerAxial->GetInteractorStyle());
        if (style) {
            if (!m_axialClickCallback) {
                m_axialClickCallback = vtkSmartPointer<vtkCallbackCommand>::New();
                m_axialClickCallback->SetCallback(Widget::OnClickCallback);
            }
            m_axialClickCallback->SetClientData(this);
            if (m_axialClickTag != 0) {
                style->RemoveObserver(m_axialClickTag);
            }
            m_axialClickTag = style->AddObserver(vtkCommand::LeftButtonPressEvent, m_axialClickCallback);
        }
    }
    if (m_viewerSagittal) {
        auto *style = vtkInteractorStyleImage::SafeDownCast(m_viewerSagittal->GetInteractorStyle());
        if (style) {
            if (!m_sagittalClickCallback) {
                m_sagittalClickCallback = vtkSmartPointer<vtkCallbackCommand>::New();
                m_sagittalClickCallback->SetCallback(Widget::OnClickCallback);
            }
            m_sagittalClickCallback->SetClientData(this);
            if (m_sagittalClickTag != 0) {
                style->RemoveObserver(m_sagittalClickTag);
            }
            m_sagittalClickTag = style->AddObserver(vtkCommand::LeftButtonPressEvent, m_sagittalClickCallback);
        }
    }
    if (m_viewerCoronal) {
        auto *style = vtkInteractorStyleImage::SafeDownCast(m_viewerCoronal->GetInteractorStyle());
        if (style) {
            if (!m_coronalClickCallback) {
                m_coronalClickCallback = vtkSmartPointer<vtkCallbackCommand>::New();
                m_coronalClickCallback->SetCallback(Widget::OnClickCallback);
            }
            m_coronalClickCallback->SetClientData(this);
            if (m_coronalClickTag != 0) {
                style->RemoveObserver(m_coronalClickTag);
            }
            m_coronalClickTag = style->AddObserver(vtkCommand::LeftButtonPressEvent, m_coronalClickCallback);
        }
    }

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
    
    std::string cleanedValue = value;
    size_t firstNonSpace = cleanedValue.find_first_not_of(" \t\n\r");
    if (firstNonSpace != std::string::npos) {
        cleanedValue.erase(0, firstNonSpace);
    }
    size_t lastNonSpace = cleanedValue.find_last_not_of(" \t\n\r");
    if (lastNonSpace != std::string::npos) {
        cleanedValue.erase(lastNonSpace + 1);
    }
    
    if (cleanedValue.empty()) {
        return "N/A";
    }
    
    if (tagKey == "0010|0010") {
        QString nameFromLocal = QString::fromLocal8Bit(cleanedValue.c_str());
        if (!nameFromLocal.contains('?') && !nameFromLocal.isEmpty()) {
            return nameFromLocal.toStdString();
        }
        
        QTextCodec *gbkCodec = QTextCodec::codecForName("GBK");
        if (gbkCodec) {
            QString nameFromGBK = gbkCodec->toUnicode(cleanedValue.c_str());
            if (!nameFromGBK.contains('?') && !nameFromGBK.isEmpty()) {
                return nameFromGBK.toStdString();
            }
        }
        
        QTextCodec *gb2312Codec = QTextCodec::codecForName("GB2312");
        if (gb2312Codec) {
            QString nameFromGB2312 = gb2312Codec->toUnicode(cleanedValue.c_str());
            if (!nameFromGB2312.contains('?') && !nameFromGB2312.isEmpty()) {
                return nameFromGB2312.toStdString();
            }
        }
    }
    
    return cleanedValue;
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
    
    if (m_maskData) {
        UpdateMaskSlice(m_viewerAxial, m_maskAxial, "Axial");
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
    
    if (m_maskData) {
        UpdateMaskSlice(m_viewerSagittal, m_maskSagittal, "Sagittal");
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
    
    if (m_maskData) {
        UpdateMaskSlice(m_viewerCoronal, m_maskCoronal, "Coronal");
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

    if (m_viewerAxial) {
        m_viewerAxial->SetColorWindow(w);
        m_viewerAxial->SetColorLevel(l);
        m_viewerAxial->Render();
    }
    if (m_viewerSagittal) {
        m_viewerSagittal->SetColorWindow(w);
        m_viewerSagittal->SetColorLevel(l);
        m_viewerSagittal->Render();
    }
    if (m_viewerCoronal) {
        m_viewerCoronal->SetColorWindow(w);
        m_viewerCoronal->SetColorLevel(l);
        m_viewerCoronal->Render();
    }

    if (m_planeAxial) {
        m_planeAxial->SetWindowLevel(w, l);
    }
    if (m_planeSagittal) {
        m_planeSagittal->SetWindowLevel(w, l);
    }
    if (m_planeCoronal) {
        m_planeCoronal->SetWindowLevel(w, l);
    }

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
                                  unsigned long,
                                  void* clientData,
                                  void*)
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

    // Update Axial
    if (m_annotAxial) {
        int sliceMin = m_viewerAxial->GetSliceMin();
        int sliceMax = m_viewerAxial->GetSliceMax();
        int totalSlices = sliceMax - sliceMin + 1;
        if (totalSlices < 1) totalSlices = 1;
        int slice = m_viewerAxial->GetSlice() - sliceMin + 1;
        if (slice < 1) slice = 1;
        if (slice > totalSlices) slice = totalSlices;

        QString patientName = QString::fromStdString(m_patientName);
        QString patientID = QString::fromStdString(m_patientID);
        if (patientName.trimmed().isEmpty() || patientName == "N/A") {
            patientName = "N/A";
        }

        QString topLeft = QString("Name: %1\nID: %2\nView: Axial").arg(patientName).arg(patientID);
        m_annotAxial->SetText(0, topLeft.toUtf8().constData());

        QString bottomLeft = QString("Slice: %1 / %2").arg(slice).arg(totalSlices);
        m_annotAxial->SetText(1, bottomLeft.toUtf8().constData());

        double w = m_viewerAxial->GetColorWindow();
        double l = m_viewerAxial->GetColorLevel();
        QString bottomRight = QString("W: %1  L: %2").arg(static_cast<int>(w)).arg(static_cast<int>(l));
        m_annotAxial->SetText(2, bottomRight.toUtf8().constData());
    }

    // Update Sagittal
    if (m_annotSagittal) {
        int sliceMin = m_viewerSagittal->GetSliceMin();
        int sliceMax = m_viewerSagittal->GetSliceMax();
        int totalSlices = sliceMax - sliceMin + 1;
        if (totalSlices < 1) totalSlices = 1;
        int slice = m_viewerSagittal->GetSlice() - sliceMin + 1;
        if (slice < 1) slice = 1;
        if (slice > totalSlices) slice = totalSlices;

        QString patientName = QString::fromStdString(m_patientName);
        QString patientID = QString::fromStdString(m_patientID);
        if (patientName.trimmed().isEmpty() || patientName == "N/A") {
            patientName = "N/A";
        }

        QString topLeft = QString("Name: %1\nID: %2\nView: Sagittal").arg(patientName).arg(patientID);
        m_annotSagittal->SetText(0, topLeft.toUtf8().constData());

        QString bottomLeft = QString("Slice: %1 / %2").arg(slice).arg(totalSlices);
        m_annotSagittal->SetText(1, bottomLeft.toUtf8().constData());

        double w = m_viewerSagittal->GetColorWindow();
        double l = m_viewerSagittal->GetColorLevel();
        QString bottomRight = QString("W: %1  L: %2").arg(static_cast<int>(w)).arg(static_cast<int>(l));
        m_annotSagittal->SetText(2, bottomRight.toUtf8().constData());
    }

    // Update Coronal
    if (m_annotCoronal) {
        int sliceMin = m_viewerCoronal->GetSliceMin();
        int sliceMax = m_viewerCoronal->GetSliceMax();
        int totalSlices = sliceMax - sliceMin + 1;
        if (totalSlices < 1) totalSlices = 1;
        int slice = m_viewerCoronal->GetSlice() - sliceMin + 1;
        if (slice < 1) slice = 1;
        if (slice > totalSlices) slice = totalSlices;

        QString patientName = QString::fromStdString(m_patientName);
        QString patientID = QString::fromStdString(m_patientID);
        if (patientName.trimmed().isEmpty() || patientName == "N/A") {
            patientName = "N/A";
        }

        QString topLeft = QString("Name: %1\nID: %2\nView: Coronal").arg(patientName).arg(patientID);
        m_annotCoronal->SetText(0, topLeft.toUtf8().constData());

        QString bottomLeft = QString("Slice: %1 / %2").arg(slice).arg(totalSlices);
        m_annotCoronal->SetText(1, bottomLeft.toUtf8().constData());

        double w = m_viewerCoronal->GetColorWindow();
        double l = m_viewerCoronal->GetColorLevel();
        QString bottomRight = QString("W: %1  L: %2").arg(static_cast<int>(w)).arg(static_cast<int>(l));
        m_annotCoronal->SetText(2, bottomRight.toUtf8().constData());
    }

    m_viewerAxial->Render();
    m_viewerSagittal->Render();
    m_viewerCoronal->Render();
}

void Widget::OnClickCallback(vtkObject* caller,
                             unsigned long,
                             void* clientData,
                             void*)
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
    picker->SetTolerance(0.005);

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

    int idxX = static_cast<int>(std::round((pickPos[0] - origin[0]) / spacing[0]));
    int idxY = static_cast<int>(std::round((pickPos[1] - origin[1]) / spacing[1]));
    int idxZ = static_cast<int>(std::round((pickPos[2] - origin[2]) / spacing[2]));

    int axialMin = m_viewerAxial ? m_viewerAxial->GetSliceMin() : 0;
    int axialMax = m_viewerAxial ? m_viewerAxial->GetSliceMax() : 0;
    if (idxZ < axialMin) idxZ = axialMin;
    if (idxZ > axialMax) idxZ = axialMax;

    int sagittalMin = m_viewerSagittal ? m_viewerSagittal->GetSliceMin() : 0;
    int sagittalMax = m_viewerSagittal ? m_viewerSagittal->GetSliceMax() : 0;
    if (idxX < sagittalMin) idxX = sagittalMin;
    if (idxX > sagittalMax) idxX = sagittalMax;

    int coronalMin = m_viewerCoronal ? m_viewerCoronal->GetSliceMin() : 0;
    int coronalMax = m_viewerCoronal ? m_viewerCoronal->GetSliceMax() : 0;
    if (idxY < coronalMin) idxY = coronalMin;
    if (idxY > coronalMax) idxY = coronalMax;

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

void Widget::onMeasureToggled(bool checked)
{
    if (checked) {
        if (!m_viewerAxial || !m_viewerSagittal || !m_viewerCoronal) {
            QMessageBox::warning(this, QStringLiteral("Warning"), QStringLiteral("Please open DICOM images first."));
            if (auto btn = ui->btn_measure) {
                btn->setChecked(false);
            }
            return;
        }

        vtkImageData *imageData = m_viewerAxial->GetInput();
        if (!imageData) {
            QMessageBox::warning(this, QStringLiteral("Warning"), QStringLiteral("Cannot get image data."));
            if (auto btn = ui->btn_measure) {
                btn->setChecked(false);
            }
            return;
        }

        double spacing[3];
        imageData->GetSpacing(spacing);

        if (!m_distWidgetAxial) {
            m_distWidgetAxial = vtkSmartPointer<vtkDistanceWidget>::New();
            auto rep = vtkSmartPointer<vtkDistanceRepresentation2D>::New();
            m_distWidgetAxial->SetRepresentation(rep);
            rep->SetLabelFormat("%-#6.2f mm");
        }
        auto axialInteractor = m_viewerAxial->GetRenderWindow() ? 
                               m_viewerAxial->GetRenderWindow()->GetInteractor() : nullptr;
        if (axialInteractor) {
            m_distWidgetAxial->SetInteractor(axialInteractor);
            m_distWidgetAxial->On();
        }

        if (!m_distWidgetSagittal) {
            m_distWidgetSagittal = vtkSmartPointer<vtkDistanceWidget>::New();
            auto rep = vtkSmartPointer<vtkDistanceRepresentation2D>::New();
            m_distWidgetSagittal->SetRepresentation(rep);
            rep->SetLabelFormat("%-#6.2f mm");
        }
        auto sagittalInteractor = m_viewerSagittal->GetRenderWindow() ? 
                                  m_viewerSagittal->GetRenderWindow()->GetInteractor() : nullptr;
        if (sagittalInteractor) {
            m_distWidgetSagittal->SetInteractor(sagittalInteractor);
            m_distWidgetSagittal->On();
        }

        if (!m_distWidgetCoronal) {
            m_distWidgetCoronal = vtkSmartPointer<vtkDistanceWidget>::New();
            auto rep = vtkSmartPointer<vtkDistanceRepresentation2D>::New();
            m_distWidgetCoronal->SetRepresentation(rep);
            rep->SetLabelFormat("%-#6.2f mm");
        }
        auto coronalInteractor = m_viewerCoronal->GetRenderWindow() ? 
                                 m_viewerCoronal->GetRenderWindow()->GetInteractor() : nullptr;
        if (coronalInteractor) {
            m_distWidgetCoronal->SetInteractor(coronalInteractor);
            m_distWidgetCoronal->On();
        }

        m_viewerAxial->Render();
        m_viewerSagittal->Render();
        m_viewerCoronal->Render();
    } else {
        if (m_distWidgetAxial) {
            m_distWidgetAxial->Off();
        }
        if (m_distWidgetSagittal) {
            m_distWidgetSagittal->Off();
        }
        if (m_distWidgetCoronal) {
            m_distWidgetCoronal->Off();
        }

        if (m_viewerAxial) {
            m_viewerAxial->Render();
        }
        if (m_viewerSagittal) {
            m_viewerSagittal->Render();
        }
        if (m_viewerCoronal) {
            m_viewerCoronal->Render();
        }
    }
}

// ===== Mask overlay implementation =====

void Widget::UpdateMaskSlice(vtkResliceImageViewer *viewer,
                             MaskPipeline &maskPipe,
                             const char *viewName)
{
    if (!viewer || !maskPipe.actor || !m_maskData) {
        return;
    }

    int sliceIndex = viewer->GetSlice();
    
    int maskDims[3];
    m_maskData->GetDimensions(maskDims);
    
    QString viewStr(viewName);
    
    // Update colorMap if needed
    if (maskPipe.colorMap) {
        maskPipe.colorMap->Update();
    }
    
    // Use SetDisplayExtent on 3D colored data to show slice
    if (viewStr == "Axial") {
        int z = std::clamp(sliceIndex, 0, maskDims[2] - 1);
        maskPipe.actor->SetDisplayExtent(0, maskDims[0] - 1, 0, maskDims[1] - 1, z, z);
    } else if (viewStr == "Sagittal") {
        int x = std::clamp(sliceIndex, 0, maskDims[0] - 1);
        maskPipe.actor->SetDisplayExtent(x, x, 0, maskDims[1] - 1, 0, maskDims[2] - 1);
    } else if (viewStr == "Coronal") {
        int y = std::clamp(sliceIndex, 0, maskDims[1] - 1);
        maskPipe.actor->SetDisplayExtent(0, maskDims[0] - 1, y, y, 0, maskDims[2] - 1);
    }

    viewer->Render();
}

static vtkSmartPointer<vtkLookupTable> CreateMaskLookupTable(double minVal, double maxVal)
{
    vtkSmartPointer<vtkLookupTable> lut = vtkSmartPointer<vtkLookupTable>::New();

    // Discrete label LUT:
    //   0 -> transparent
    //   1 -> red
    //   2 -> green
    //   3 -> blue
    //   others -> yellow
    lut->SetNumberOfTableValues(4);
    lut->SetRange(0, 3);

    // 0: background - fully transparent
    lut->SetTableValue(0, 0.0, 0.0, 0.0, 0.0);
    // 1: label 1 - red
    lut->SetTableValue(1, 1.0, 0.0, 0.0, 0.7);
    // 2: label 2 - green
    lut->SetTableValue(2, 0.0, 1.0, 0.0, 0.7);
    // 3: label 3 - blue
    lut->SetTableValue(3, 0.0, 0.0, 1.0, 0.7);

    lut->Build();
    
    return lut;
}

void Widget::SetupMaskPipeline()
{
    if (!m_maskData) {
        return;
    }

    double range[2];
    m_maskData->GetScalarRange(range);
    vtkSmartPointer<vtkLookupTable> lut = CreateMaskLookupTable(range[0], range[1]);

    // Create shared colorMap for 3D mask data
    vtkSmartPointer<vtkImageMapToColors> colorMap3D = vtkSmartPointer<vtkImageMapToColors>::New();
    colorMap3D->SetInputData(m_maskData);
    colorMap3D->SetLookupTable(lut);
    colorMap3D->SetOutputFormatToRGBA();
    colorMap3D->PassAlphaToOutputOn();
    colorMap3D->Update();
    
    vtkImageData* coloredMask = colorMap3D->GetOutput();

    // Axial - use SetDisplayExtent on 3D colored data
    if (m_viewerAxial) {
        if (!m_maskAxial.colorMap) {
            m_maskAxial.colorMap = colorMap3D;  // Share the colorMap
        }

        if (!m_maskAxial.actor) {
            m_maskAxial.actor = vtkSmartPointer<vtkImageActor>::New();
        }
        m_maskAxial.actor->SetInputData(coloredMask);
        m_maskAxial.actor->GetProperty()->SetOpacity(1.0);
        m_maskAxial.actor->PickableOff();
        m_maskAxial.actor->SetVisibility(1);

        vtkRenderer *renderer = m_viewerAxial->GetRenderer();
        if (renderer && !renderer->HasViewProp(m_maskAxial.actor)) {
            renderer->AddActor(m_maskAxial.actor);
        }
    }

    // Sagittal - use SetDisplayExtent on 3D colored data
    if (m_viewerSagittal) {
        if (!m_maskSagittal.colorMap) {
            m_maskSagittal.colorMap = colorMap3D;  // Share the colorMap
        }

        if (!m_maskSagittal.actor) {
            m_maskSagittal.actor = vtkSmartPointer<vtkImageActor>::New();
        }
        m_maskSagittal.actor->SetInputData(coloredMask);
        m_maskSagittal.actor->GetProperty()->SetOpacity(1.0);
        m_maskSagittal.actor->PickableOff();
        m_maskSagittal.actor->SetVisibility(1);

        vtkRenderer *renderer = m_viewerSagittal->GetRenderer();
        if (renderer && !renderer->HasViewProp(m_maskSagittal.actor)) {
            renderer->AddActor(m_maskSagittal.actor);
        }
    }

    // Coronal - use SetDisplayExtent on 3D colored data
    if (m_viewerCoronal) {
        if (!m_maskCoronal.colorMap) {
            m_maskCoronal.colorMap = colorMap3D;  // Share the colorMap
        }

        if (!m_maskCoronal.actor) {
            m_maskCoronal.actor = vtkSmartPointer<vtkImageActor>::New();
        }
        m_maskCoronal.actor->SetInputData(coloredMask);
        m_maskCoronal.actor->GetProperty()->SetOpacity(1.0);
        m_maskCoronal.actor->PickableOff();
        m_maskCoronal.actor->SetVisibility(1);

        vtkRenderer *renderer = m_viewerCoronal->GetRenderer();
        if (renderer && !renderer->HasViewProp(m_maskCoronal.actor)) {
            renderer->AddActor(m_maskCoronal.actor);
        }
    }

    UpdateMaskSlice(m_viewerAxial, m_maskAxial, "Axial");
    UpdateMaskSlice(m_viewerSagittal, m_maskSagittal, "Sagittal");
    UpdateMaskSlice(m_viewerCoronal, m_maskCoronal, "Coronal");
}

void Widget::onLoadMask()
{
    if (!m_viewerAxial || !m_viewerSagittal || !m_viewerCoronal) {
        QMessageBox::warning(this, QStringLiteral("Warning"), QStringLiteral("Please open DICOM images first."));
        return;
    }

    vtkImageData *baseImage = m_viewerAxial->GetInput();
    if (!baseImage) {
        QMessageBox::warning(this, QStringLiteral("Warning"), QStringLiteral("Cannot get base image data."));
        return;
    }

    QString maskPath = QFileDialog::getOpenFileName(this, QStringLiteral("Select Mask File"), QString(),
        QStringLiteral("NIfTI (*.nii *.nii.gz);;MetaImage (*.mha *.mhd);;All (*.*)"));

    if (maskPath.isEmpty()) {
        return;
    }

    vtkSmartPointer<vtkImageData> maskVtk;

    try {
        typedef itk::Image<unsigned char, Dimension> MaskTypeUChar;
        typedef itk::ImageFileReader<MaskTypeUChar> ReaderTypeUChar;
        ReaderTypeUChar::Pointer reader = ReaderTypeUChar::New();
        reader->SetFileName(maskPath.toStdString());
        reader->Update();

        MaskTypeUChar::Pointer itkImage = reader->GetOutput();
        MaskTypeUChar::RegionType region = itkImage->GetLargestPossibleRegion();
        MaskTypeUChar::SizeType size = region.GetSize();
        MaskTypeUChar::SpacingType spacing = itkImage->GetSpacing();
        MaskTypeUChar::PointType origin = itkImage->GetOrigin();

        maskVtk = vtkSmartPointer<vtkImageData>::New();
        maskVtk->SetDimensions(static_cast<int>(size[0]), static_cast<int>(size[1]), static_cast<int>(size[2]));
        maskVtk->SetSpacing(spacing[0], spacing[1], spacing[2]);
        maskVtk->SetOrigin(origin[0], origin[1], origin[2]);
        maskVtk->AllocateScalars(VTK_UNSIGNED_CHAR, 1);

        std::memcpy(maskVtk->GetScalarPointer(), itkImage->GetBufferPointer(), region.GetNumberOfPixels() * sizeof(unsigned char));
    } catch (...) {
        try {
            typedef itk::Image<short, Dimension> MaskTypeShort;
            typedef itk::ImageFileReader<MaskTypeShort> ReaderTypeShort;
            ReaderTypeShort::Pointer reader = ReaderTypeShort::New();
            reader->SetFileName(maskPath.toStdString());
            reader->Update();

            MaskTypeShort::Pointer itkImage = reader->GetOutput();
            MaskTypeShort::RegionType region = itkImage->GetLargestPossibleRegion();
            MaskTypeShort::SizeType size = region.GetSize();
            MaskTypeShort::SpacingType spacing = itkImage->GetSpacing();
            MaskTypeShort::PointType origin = itkImage->GetOrigin();

            maskVtk = vtkSmartPointer<vtkImageData>::New();
            maskVtk->SetDimensions(static_cast<int>(size[0]), static_cast<int>(size[1]), static_cast<int>(size[2]));
            maskVtk->SetSpacing(spacing[0], spacing[1], spacing[2]);
            maskVtk->SetOrigin(origin[0], origin[1], origin[2]);
            maskVtk->AllocateScalars(VTK_SHORT, 1);

            std::memcpy(maskVtk->GetScalarPointer(), itkImage->GetBufferPointer(), region.GetNumberOfPixels() * sizeof(short));
        } catch (...) {
        }
    }

    if (!maskVtk) {
        QMessageBox::warning(this, QStringLiteral("Error"), QStringLiteral("Failed to read mask file."));
        return;
    }

    int baseDims[3];
    int maskDims[3];
    baseImage->GetDimensions(baseDims);
    maskVtk->GetDimensions(maskDims);

    if (baseDims[0] != maskDims[0] || baseDims[1] != maskDims[1] || baseDims[2] != maskDims[2]) {
        QString msg = QString("Mask dimensions mismatch. Base: %1x%2x%3 Mask: %4x%5x%6")
            .arg(baseDims[0]).arg(baseDims[1]).arg(baseDims[2])
            .arg(maskDims[0]).arg(maskDims[1]).arg(maskDims[2]);
        QMessageBox::warning(this, QStringLiteral("Warning"), msg);
    }

    double baseOrigin[3];
    double baseSpacing[3];
    baseImage->GetOrigin(baseOrigin);
    baseImage->GetSpacing(baseSpacing);
    maskVtk->SetOrigin(baseOrigin);
    maskVtk->SetSpacing(baseSpacing);

    if (m_maskAxial.actor && m_viewerAxial && m_viewerAxial->GetRenderer()) {
        m_viewerAxial->GetRenderer()->RemoveActor(m_maskAxial.actor);
        m_maskAxial.actor = nullptr;
        m_maskAxial.extractVOI = nullptr;
        m_maskAxial.colorMap = nullptr;
    }
    if (m_maskSagittal.actor && m_viewerSagittal && m_viewerSagittal->GetRenderer()) {
        m_viewerSagittal->GetRenderer()->RemoveActor(m_maskSagittal.actor);
        m_maskSagittal.actor = nullptr;
        m_maskSagittal.extractVOI = nullptr;
        m_maskSagittal.colorMap = nullptr;
    }
    if (m_maskCoronal.actor && m_viewerCoronal && m_viewerCoronal->GetRenderer()) {
        m_viewerCoronal->GetRenderer()->RemoveActor(m_maskCoronal.actor);
        m_maskCoronal.actor = nullptr;
        m_maskCoronal.extractVOI = nullptr;
        m_maskCoronal.colorMap = nullptr;
    }

    m_maskData = maskVtk;
    SetupMaskPipeline();


    if (m_viewerAxial) m_viewerAxial->Render();
    if (m_viewerSagittal) m_viewerSagittal->Render();
    if (m_viewerCoronal) m_viewerCoronal->Render();

    QMessageBox::information(this, QStringLiteral("Success"), 
        QString("Mask loaded successfully!\nDimensions: %1 x %2 x %3")
        .arg(maskDims[0]).arg(maskDims[1]).arg(maskDims[2]));
}
