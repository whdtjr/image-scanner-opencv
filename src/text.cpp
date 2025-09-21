#include <opencv2/opencv.hpp>
#include <iostream>
using namespace cv;


Mat postProcess(const Mat& edges) {
    Mat out = edges.clone();
    Mat k = getStructuringElement(MORPH_RECT, Size(3,3));
    dilate(out, out, k, Point(-1,-1), 1); // 굵게
    erode(out, out, k, Point(-1,-1), 1);  // 노이즈 정리
    return out;
}

Mat laplaceEdges(const Mat& bgr) {
    Mat gray; cvtColor(bgr, gray, COLOR_BGR2GRAY);
    Mat blurImg; GaussianBlur(gray, blurImg, Size(3,3), 0.8);
    Mat lap; Laplacian(blurImg, lap, CV_16S, 3);
    Mat absLap; convertScaleAbs(lap, absLap);
    Mat edges; threshold(absLap, edges, 30, 255, THRESH_BINARY);
    return edges;
}
Mat sobelMag(const Mat& bgr) {
    Mat gray; cvtColor(bgr, gray, COLOR_BGR2GRAY);
    Mat gx, gy; // 16비트 부호 정수로 그래디언트
    Sobel(gray, gx, CV_16S, 1, 0, 3);
    Sobel(gray, gy, CV_16S, 0, 1, 3);
    Mat absx, absy; convertScaleAbs(gx, absx); convertScaleAbs(gy, absy);
    Mat mag; addWeighted(absx, 0.5, absy, 0.5, 0, mag); // 근사 크기
    // 임계값으로 바이너리 엣지화
    Mat edges; threshold(mag, edges, 50, 255, THRESH_BINARY);
    return edges;
}
Mat cannyAuto(const Mat& bgr) {
    Mat gray, blurImg, edges;
    cvtColor(bgr, gray, COLOR_BGR2GRAY);
    GaussianBlur(gray, blurImg, Size(5,5), 1.2);

    // ✅ rvalue Mat() 대신 변수로 받기
    Mat tmp; 
    double otsu = threshold(blurImg, tmp, 0, 255, THRESH_BINARY | THRESH_OTSU);

    double t1 = std::max(5.0, 0.5 * otsu);
    double t2 = 1.5 * otsu;

    Canny(blurImg, edges, t1, t2, 3, true);
    return edges;
}

// 간단한 캐니 엣지
Mat cannyEdges(const Mat& bgr, double t1=60, double t2=180) {
    Mat gray, blurImg, edges;
    cvtColor(bgr, gray, COLOR_BGR2GRAY);
    GaussianBlur(gray, blurImg, Size(5,5), 1.2);
    Canny(blurImg, edges, t1, t2); // t1=low, t2=high
    return edges;
}

int main(int argc, char** argv){
    if(argc < 2){ std::cout << "usage: app image.jpg\n"; return 0; }
    Mat img = imread(argv[1]);
    if(img.empty()){ std::cerr << "fail to read image\n"; return -1; }

    Mat edges1 = cannyEdges(img, 60, 180);
    Mat edges2 = cannyAuto(img);
    Mat edges3 = sobelMag(img);
    Mat edges4 = laplaceEdges(img);

    imwrite("edges_canny.jpg", edges1);
    imwrite("edges_canny_auto.jpg", edges2);
    imwrite("edges_sobel.jpg", postProcess(edges3));
    imwrite("edges_laplace.jpg", postProcess(edges4));

    // 미리보기
    // imshow("canny", edges1);
    // imshow("canny auto", edges2);
    // imshow("sobel", postProcess(edges3));
    // imshow("laplace", postProcess(edges4));
    // waitKey(0);
    return 0;
}
