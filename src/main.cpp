#include <opencv2/opencv.hpp>
#include <iostream>
#include <algorithm>

using namespace cv;
using namespace std;

// 두 점 사이 거리
static float dist2f(const Point2f& a, const Point2f& b) {
    return sqrtf((a.x - b.x)*(a.x - b.x) + (a.y - b.y)*(a.y - b.y));
}

// 꼭짓점 4개를 [TL, TR, BR, BL] 순서로 정렬
static array<Point2f,4> orderCorners(const vector<Point>& quad) {
    // 입력은 꼭짓점 4개라고 가정 (approxPolyDP 결과)
    vector<Point2f> pts;
    for (auto& p : quad) pts.emplace_back((float)p.x, (float)p.y);

    // 무게중심
    Point2f c(0,0);
    for (auto& p : pts) c += p;
    c *= (1.0f / (float)pts.size());

    array<Point2f,4> ordered; // TL, TR, BR, BL
    for (auto& p : pts) {
        if      (p.x < c.x && p.y < c.y) ordered[0] = p; // TL
        else if (p.x > c.x && p.y < c.y) ordered[1] = p; // TR
        else if (p.x > c.x && p.y > c.y) ordered[2] = p; // BR
        else                             ordered[3] = p; // BL
    }

    // 혹시 뒤집혔을 때 보정 (시계/반시계 방향 혼동 방지)
    // 대각선 길이로 간단 검증: TL-BR, TR-BL
    float d1 = dist2f(ordered[0], ordered[2]);
    float d2 = dist2f(ordered[1], ordered[3]);
    if (d1 < d2) {
        // TL/TR 스왑하고 BR/BL 스왑
        swap(ordered[0], ordered[1]);
        swap(ordered[2], ordered[3]);
    }
    return ordered;
}

// 사각형(혹은 최소사각형)에서 Warp 수행
static bool warpDocument(const Mat& src, Mat& warpedColor, Mat& warpedBW) {
    // 1) 전처리: 그레이 & 블러 & 엣지
    Mat gray, blurImg, edges;
    cvtColor(src, gray, COLOR_BGR2GRAY);
    GaussianBlur(gray, blurImg, Size(5,5), 0);
    Canny(blurImg, edges, 60, 180);

    // 팽창/침식으로 외곽선 선명화
    Mat kernel = getStructuringElement(MORPH_RECT, Size(3,3));
    dilate(edges, edges, kernel);
    erode(edges, edges, kernel);

    // 2) 컨투어 탐색
    vector<vector<Point>> contours;
    findContours(edges, contours, RETR_LIST, CHAIN_APPROX_SIMPLE);

    // 가장 큰 사각형 후보 찾기
    double bestArea = 0.0;
    vector<Point> bestQuad;

    for (auto& c : contours) {
        double peri = arcLength(c, true);
        vector<Point> approx;
        approxPolyDP(c, approx, 0.02 * peri, true);

        if (approx.size() == 4 && isContourConvex(approx)) {
            double area = fabs(contourArea(approx));
            if (area > bestArea) {
                bestArea = area;
                bestQuad = approx;
            }
        }
    }

    array<Point2f,4> srcPts;
    if (!bestQuad.empty()) {
        // 3) 꼭짓점 정렬
        srcPts = orderCorners(bestQuad);
    } else {
        // 사각형을 못 찾으면 최소 외접 사각형(minAreaRect)로 폴백
        vector<Point> allPts;
        allPts.reserve(10000);
        for (auto& c : contours) {
            allPts.insert(allPts.end(), c.begin(), c.end());
        }
        if (allPts.size() < 4) return false;

        RotatedRect rr = minAreaRect(allPts);
        Point2f box[4];
        rr.points(box);

        vector<Point> poly;
        for (int i=0;i<4;++i) poly.emplace_back((int)box[i].x, (int)box[i].y);
        srcPts = orderCorners(poly);
    }

    // 4) 대상 평면 크기 계산 (문서 가로/세로)
    float widthA  = dist2f(srcPts[2], srcPts[3]); // BR-BL
    float widthB  = dist2f(srcPts[1], srcPts[0]); // TR-TL
    float maxW    = max(widthA, widthB);

    float heightA = dist2f(srcPts[1], srcPts[2]); // TR-BR
    float heightB = dist2f(srcPts[0], srcPts[3]); // TL-BL
    float maxH    = max(heightA, heightB);

    // 너무 큰 해상도 방지 (선택) - 2000px 한도
    const float MAX_SIDE = 2000.0f;
    float scale = 1.0f;
    if (max(maxW, maxH) > MAX_SIDE) {
        scale = MAX_SIDE / max(maxW, maxH);
    }
    int dstW = max( (int)(maxW * scale),  200);
    int dstH = max( (int)(maxH * scale),  200);

    array<Point2f,4> dstPts = {
        Point2f(0.f,   0.f),         // TL
        Point2f((float)dstW-1, 0.f), // TR
        Point2f((float)dstW-1,(float)dstH-1), // BR
        Point2f(0.f,   (float)dstH-1)        // BL
    };

    // 5) 투시 변환
    Mat M = getPerspectiveTransform(srcPts.data(), dstPts.data());
    warpPerspective(src, warpedColor, M, Size(dstW, dstH), INTER_CUBIC, BORDER_REPLICATE);

    // 6) 스캐너 스타일 이진화(옵션)
    Mat grayWarp;
    cvtColor(warpedColor, grayWarp, COLOR_BGR2GRAY);

    // 대비 향상 (가벼운 히스토그램 평활화)
    Mat eq; equalizeHist(grayWarp, eq);

    // 조명에 강한 적응형 임계값
    adaptiveThreshold(eq, warpedBW, 255, ADAPTIVE_THRESH_GAUSSIAN_C, THRESH_BINARY, 21, 10);

    return true;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        cout << "사용법: " << argv[0] << " input.jpg [out_prefix]\n";
        return 0;
    }
    string inPath = argv[1];
    string outPrefix = (argc >= 3) ? argv[2] : "scanned";

    Mat img = imread(inPath);
    if (img.empty()) {
        cerr << "이미지를 열 수 없습니다: " << inPath << "\n";
        return -1;
    }

    Mat warpedColor, warpedBW;
    bool ok = warpDocument(img, warpedColor, warpedBW);
    if (!ok) {
        cerr << "문서 형태를 찾지 못했습니다 :(\n";
        return -2;
    }

    imwrite(outPrefix + "_color.jpg", warpedColor);
    imwrite(outPrefix + "_bw.jpg",    warpedBW);

    cout << "완료! 저장:\n  " << outPrefix << "_color.jpg\n  " << outPrefix << "_bw.jpg\n";

    // 보기용
    // imshow("warped color", warpedColor);
    // imshow("warped bw", warpedBW);
    // waitKey(0);

    return 0;
}
