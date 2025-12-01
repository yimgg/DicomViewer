#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>

#include <vtkSmartPointer.h>
#include <vtkResliceImageViewer.h>
#include <vtkImageData.h>

#include <itkImage.h>

QT_BEGIN_NAMESPACE
namespace Ui {
class Widget;
}
QT_END_NAMESPACE

// VTK 前向声明
class QVTKOpenGLNativeWidget;
class vtkGenericOpenGLRenderWindow;
class vtkRenderer;

class Widget : public QWidget
{
    Q_OBJECT

public:
    Widget(QWidget *parent = nullptr);
    ~Widget();

private slots:
    void onOpenDicom();

private:
    using PixelType = short;
    static constexpr unsigned int Dimension = 3;
    using ImageType = itk::Image<PixelType, Dimension>;

    vtkSmartPointer<vtkImageData> ItkToVtkImage(ImageType *image);

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
};
#endif // WIDGET_H
