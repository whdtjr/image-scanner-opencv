#include "widget.h"
#include "ui_widget.h"
#include <QFileDialog>
#include <QPixmap>
#include <QMessageBox>
#include <QProcess>
#include <QFileInfo>
#include <QApplication>

Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
{
    ui->setupUi(this);
    ui->statusLabel->setText("상태: 대기 중");
}

Widget::~Widget()
{
    delete ui;
}

void Widget::on_openButton_clicked()
{
    // 파일 선택 다이얼로그 띄우기
    QString fileName = QFileDialog::getOpenFileName(
        this,
        tr("이미지 열기"),
        QDir::homePath(),
        tr("이미지 파일 (*.png *.jpg *.jpeg *.bmp *.gif)")
    );

    if (!fileName.isEmpty()) {
        QPixmap pixmap(fileName);
        if (pixmap.isNull()) {
            QMessageBox::warning(this, "오류", "이미지를 불러올 수 없습니다.");
            ui->statusLabel->setText("상태: 이미지 로드 실패");
            return;
        }

        // 선택된 파일 경로 저장
        currentImagePath = fileName;

        // 원본 이미지는 라벨 크기에 맞게 표시
        ui->labelOriginalImage->setPixmap(
            pixmap.scaled(ui->labelOriginalImage->size(),
                          Qt::KeepAspectRatio,
                          Qt::SmoothTransformation)
        );
        ui->labelOriginalImage->setAlignment(Qt::AlignCenter);

        ui->statusLabel->setText("상태: 원본 이미지 불러옴");
    }
}

void Widget::on_ocrButton_clicked()
{
    if (currentImagePath.isEmpty()) {
        QMessageBox::warning(this, "경고", "먼저 이미지를 선택하세요.");
        return;
    }

    // OCR 실행파일 경로
    QString program = "/home/ubuntu/workspace/opencv_project/ocr";

    // OCR 실행파일이 위치한 디렉토리
    QFileInfo programInfo(program);
    QString programDir = programInfo.absolutePath();

    // 실행 인자: 원본 이미지 경로
    QStringList arguments;
    arguments << currentImagePath;

    // 상태 갱신
    ui->statusLabel->setText("상태: OCR 실행 중...");

    // OCR 실행 (비동기)
    QProcess *process = new QProcess(this);
    process->setWorkingDirectory(programDir);

    connect(process,
            static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this,
            [=](int exitCode, QProcess::ExitStatus status) {
                QString output = programDir + "/final_clean.jpg";
                QPixmap ocrPixmap(output);

                if (ocrPixmap.isNull()) {
                    QMessageBox::warning(this, "오류", "OCR 결과 이미지를 불러올 수 없습니다.");
                    ui->statusLabel->setText("상태: OCR 실패");
                    process->deleteLater();
                    return;
                }

                ui->labelOcrImage->setPixmap(ocrPixmap);
                ui->labelOcrImage->setAlignment(Qt::AlignCenter);

                ui->statusLabel->setText("상태: OCR 결과 표시됨");

                process->deleteLater();
            });

    process->start(program, arguments);
}
