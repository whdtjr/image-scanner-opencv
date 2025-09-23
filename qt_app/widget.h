#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>

QT_BEGIN_NAMESPACE
namespace Ui { class Widget; }
QT_END_NAMESPACE

class Widget : public QWidget
{
    Q_OBJECT

public:
    Widget(QWidget *parent = nullptr);
    ~Widget();

private slots:
    void on_openButton_clicked();
    void on_ocrButton_clicked();

private:
    Ui::Widget *ui;
    QString currentImagePath;   // 선택된 원본 이미지 경로 저장
};

#endif // WIDGET_H
