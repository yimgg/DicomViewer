#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>

#include <vtkSmartPointer.h>
#include <vtkResliceImageViewer.h>
#include <vtkImageData.h>
#include <vtkCallbackCommand.h>
#include <vtkCornerAnnotation.h>
#include <vtkCellPicker.h>

#include <itkImage.h>
#include <itkMetaDataObject.h>

#include <string>

QT_BEGIN_NAMESPACE
namespace Ui {
class Widget;
}
QT_END_NAMESPACE

// VTK 前向声明
class QVTKOpenGLNativeWidget;
class vtkGenericOpenGLRenderWindow;
class vtkRenderer;
class vtkObject;
class vtkImagePlaneWidget;
class vtkActor;

class QSlider;

class Widget : public QWidget
{
    Q_OBJECT

public:
    Widget(QWidget *parent = nullptr);
    ~Widget();

private slots:
    void onOpenDicom();
    void onSliderAxialChanged(int value);
    void onSliderSagittalChanged(int value);
    void onSliderCoronalChanged(int value);
    void onWindowLevelChanged();

private:
    using PixelType = short;
    static constexpr unsigned int Dimension = 3;
    using ImageType = itk::Image<PixelType, Dimension>;

    vtkSmartPointer<vtkImageData> ItkToVtkImage(ImageType *image);
    std::string GetDicomValue(const itk::MetaDataDictionary &dict,
                              const std::string &tagKey) const;
    void registerSliceObserver(vtkResliceImageViewer *viewer,
                               vtkSmartPointer<vtkCallbackCommand> &callback,
                               unsigned long &observerTag);
    void handleSliceInteraction(vtkObject *caller);
    void syncSliderWithViewer(QSlider *slider, vtkResliceImageViewer *viewer);
    static void SliceChangedCallback(vtkObject* caller,
                                     unsigned long eventId,
                                     void* clientData,
                                     void* callData);

    Ui::Widget *ui;
    
    // VTK Widget 指针
    QVTKOpenGLNativeWidget *view_axial;
    QVTKOpenGLNativeWidget *view_sagittal;
    QVTKOpenGLNativeWidget *view_coronal;
    QVTKOpenGLNativeWidget *view_3d;
    
    // VTK 渲染窗口指针
    vtkGenericOpenGLRenderWindow *renderWindow_axial;
    vtkGenericOpenGLRenderWindow *renderWindow_sagittal;
    vtkGenericOpenGLRenderWindow *renderWindow_coronal;
    vtkGenericOpenGLRenderWindow *renderWindow_3d;
    
    // VTK 渲染器指针
    vtkRenderer *renderer_axial;
    vtkRenderer *renderer_sagittal;
    vtkRenderer *renderer_coronal;
    vtkRenderer *renderer_3d;

    vtkSmartPointer<vtkResliceImageViewer> m_viewerAxial;
    vtkSmartPointer<vtkResliceImageViewer> m_viewerSagittal;
    vtkSmartPointer<vtkResliceImageViewer> m_viewerCoronal;

    // 2D 视图角标
    vtkSmartPointer<vtkCornerAnnotation> m_annotAxial;
    vtkSmartPointer<vtkCornerAnnotation> m_annotSagittal;
    vtkSmartPointer<vtkCornerAnnotation> m_annotCoronal;

    // 3D 视图中的三个切片平面
    vtkSmartPointer<vtkImagePlaneWidget> m_planeAxial;
    vtkSmartPointer<vtkImagePlaneWidget> m_planeSagittal;
    vtkSmartPointer<vtkImagePlaneWidget> m_planeCoronal;

    // 3D 视图中的立方体外框
    vtkSmartPointer<vtkActor> m_outlineActor;

    vtkSmartPointer<vtkCallbackCommand> m_axialSliceCallback;
    vtkSmartPointer<vtkCallbackCommand> m_sagittalSliceCallback;
    vtkSmartPointer<vtkCallbackCommand> m_coronalSliceCallback;
    vtkSmartPointer<vtkCallbackCommand> m_axialClickCallback;
    vtkSmartPointer<vtkCallbackCommand> m_sagittalClickCallback;
    vtkSmartPointer<vtkCallbackCommand> m_coronalClickCallback;

    unsigned long m_axialObserverTag;
    unsigned long m_sagittalObserverTag;
    unsigned long m_coronalObserverTag;
    unsigned long m_axialClickTag;
    unsigned long m_sagittalClickTag;
    unsigned long m_coronalClickTag;

    // DICOM 元数据缓存
    std::string m_patientName;
    std::string m_patientID;

    void UpdateAnnotations();
    static void OnClickCallback(vtkObject* caller,
                                unsigned long eventId,
                                void* clientData,
                                void* callData);
    void HandleViewClick(vtkResliceImageViewer *viewer,
                         vtkRenderWindowInteractor *interactor,
                         vtkRenderer *renderer);
};
#endif // WIDGET_H
