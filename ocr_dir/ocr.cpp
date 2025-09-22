#include <opencv2/opencv.hpp>
#include <algorithm>
#include <iostream>
#include <vector>
using namespace std;
using namespace cv;

/* ========== 0) 선택: 카메라 왜곡 보정 (camera.yml 있으면 적용) ========== */
static bool undistortIfHaveCalib(const std::string& yaml, cv::Mat& img) {
    FileStorage fs(yaml, FileStorage::READ);
    if (!fs.isOpened()) return false;
    Mat K, D; fs["camera_matrix"] >> K; fs["dist_coeffs"] >> D;
    if (K.empty() || D.empty()) return false;
    Mat und; undistort(img, und, K, D);
    img = und;
    return true;
}

/* ========== 1) 유틸 ========== */
static void autoCanny(const Mat& gray, Mat& edges, double sigma=0.33){
    CV_Assert(gray.channels()==1);
    Mat flat = gray.reshape(0,1);
    vector<uchar> v; v.assign(flat.datastart, flat.dataend);
    nth_element(v.begin(), v.begin()+v.size()/2, v.end());
    double med = v[v.size()/2];
    double low  = max(0.0,  (1.0 - sigma) * med);
    double high = min(255.0, (1.0 + sigma) * med);
    Canny(gray, edges, low, high);
}

static vector<Point2f> orderQuad(const vector<Point2f>& in){
    CV_Assert(in.size()==4);
    vector<Point2f> p = in;
    sort(p.begin(), p.end(), [](auto&a, auto&b){ return a.y < b.y; });
    Point2f tl = (p[0].x < p[1].x) ? p[0] : p[1];
    Point2f tr = (p[0].x > p[1].x) ? p[0] : p[1];
    Point2f bl = (p[2].x < p[3].x) ? p[2] : p[3];
    Point2f br = (p[2].x > p[3].x) ? p[2] : p[3];
    return {tl,tr,br,bl};
}

static bool approxToQuad(const vector<Point>& contour, vector<Point2f>& quad){
    vector<Point> hull; convexHull(contour, hull, true);
    double peri = arcLength(hull, true);
    for(double k=0.01; k<=0.05; k+=0.005){
        vector<Point> ap;
        approxPolyDP(hull, ap, peri*k, true);
        if(ap.size()==4 && isContourConvex(ap)){
            quad.clear();
            for(auto&p: ap) quad.emplace_back((float)p.x,(float)p.y);
            quad = orderQuad(quad);
            return true;
        }
    }
    return false;
}

/* ========== 2) 종이 마스크 (HSV + Lab 중성 동시 조건) ========== */
static void makePaperMaskHSV(const Mat& img, Mat& mask){
    Mat hsv; cvtColor(img, hsv, COLOR_BGR2HSV);
    Mat mask_hsv;
    inRange(hsv, Scalar(0, 0, 170), Scalar(179, 60, 255), mask_hsv);

    Mat lab; cvtColor(img, lab, COLOR_BGR2Lab);
    vector<Mat> ch; split(lab, ch);
    Mat L = ch[0], A = ch[1], B = ch[2];
    Mat Lmask, Adev, Bdev, Amask, Bmask;
    threshold(L, Lmask, 180, 255, THRESH_BINARY);
    absdiff(A, Scalar(128), Adev);
    absdiff(B, Scalar(128), Bdev);
    threshold(Adev, Amask, 12, 255, THRESH_BINARY_INV);
    threshold(Bdev, Bmask, 12, 255, THRESH_BINARY_INV);

    bitwise_and(Lmask, Amask, mask);
    bitwise_and(mask, Bmask, mask);
    bitwise_and(mask, mask_hsv, mask);

    Mat k7 = getStructuringElement(MORPH_RECT, Size(7,7));
    Mat k5 = getStructuringElement(MORPH_RECT, Size(5,5));
    morphologyEx(mask, mask, MORPH_CLOSE, k7);
    morphologyEx(mask, mask, MORPH_OPEN,  k5);
}

/* ========== 3) 문서 사각형 탐지 ========== */
static bool findQuadByPaperMask(const Mat& img, vector<Point2f>& quad){
    Mat mask; makePaperMaskHSV(img, mask);
    vector<vector<Point>> cs; findContours(mask, cs, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);
    if(cs.empty()) return false;
    size_t bestIdx=0; double bestA=0;
    for(size_t i=0;i<cs.size();++i){
        double A=fabs(contourArea(cs[i])); if(A>bestA){bestA=A; bestIdx=i;}
    }
    if(approxToQuad(cs[bestIdx], quad)) return true;

    RotatedRect rr = minAreaRect(cs[bestIdx]);
    Point2f rp[4]; rr.points(rp);
    vector<Point2f> q(rp, rp+4); quad = orderQuad(q);
    return true;
}

static bool findQuadByEdgesMasked(const Mat& img, vector<Point2f>& bestQuad){
    Mat gray; cvtColor(img, gray, COLOR_BGR2GRAY);
    GaussianBlur(gray, gray, Size(5,5), 0);
    Ptr<CLAHE> clahe = createCLAHE(3.0, Size(8,8)); clahe->apply(gray, gray);
    Mat edges; autoCanny(gray, edges);

    Mat mask; makePaperMaskHSV(img, mask);
    bitwise_and(edges, mask, edges);

    Mat k = getStructuringElement(MORPH_RECT, Size(5,5));
    morphologyEx(edges, edges, MORPH_CLOSE, k);
    dilate(edges, edges, k);

    vector<vector<Point>> cs; findContours(edges, cs, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);
    if(cs.empty()) return false;

    double imgA = (double)img.cols * img.rows;
    double bestA=0;
    for(auto& c: cs){
        double A=fabs(contourArea(c));
        if(A < imgA*0.04 || A > imgA*0.99) continue;
        vector<Point2f> q;
        if(approxToQuad(c, q)){
            double qA = fabs(contourArea(vector<Point>{(Point)q[0],(Point)q[1],(Point)q[2],(Point)q[3]}));
            if(qA>bestA){ bestA=qA; bestQuad=q; }
        }
    }
    return bestA>0;
}

static bool detectDocumentQuad(const Mat& img, vector<Point2f>& quad){
    if(findQuadByPaperMask(img, quad)) return true;
    return findQuadByEdgesMasked(img, quad);
}

/* ========== 4) A4 비율로 워핑 ========== */
static Mat warpToA4(const Mat& img, const vector<Point2f>& q, int target_long_px=3508){
    CV_Assert(q.size()==4);
    float w1 = (float)norm(q[1]-q[0]), w2 = (float)norm(q[2]-q[3]);
    float h1 = (float)norm(q[3]-q[0]), h2 = (float)norm(q[2]-q[1]);
    bool portrait = max(h1,h2) >= max(w1,w2);

    const double A4_RATIO = 297.0/210.0;
    int H = portrait ? target_long_px : (int)std::round(target_long_px / A4_RATIO);
    int W = portrait ? (int)std::round(target_long_px / A4_RATIO) : target_long_px;

    vector<Point2f> dst = { {0,0}, {(float)W-1,0}, {(float)W-1,(float)H-1}, {0,(float)H-1} };
    Mat M = getPerspectiveTransform(q, dst), out;
    warpPerspective(img, out, M, Size(W,H), INTER_CUBIC);
    return out;
}

/* ========== 5) 스캐너풍 보정 ========== */
static Mat makeScanLook(const Mat& bgr){
    Mat gray; cvtColor(bgr, gray, COLOR_BGR2GRAY);
    Mat f, bg, normed; gray.convertTo(f, CV_32F, 1.0/255);
    GaussianBlur(f, bg, Size(0,0), 21);
    divide(f, bg + 1e-3f, normed);
    normalize(normed, normed, 0, 255, NORM_MINMAX);
    normed.convertTo(gray, CV_8U);
    Ptr<CLAHE> clahe = createCLAHE(3.0, Size(8,8));
    clahe->apply(gray, gray);
    return gray;
}

/* ========== 6) 글씨만 남기고 배경 흰색 처리 ========== */
static Mat makeTextOnWhite(const Mat& bgr){
    Mat gray = makeScanLook(bgr);
    Mat bin;
    adaptiveThreshold(gray, bin, 255, ADAPTIVE_THRESH_GAUSSIAN_C, THRESH_BINARY, 31, 10);
    Mat k = getStructuringElement(MORPH_RECT, Size(2,2));
    morphologyEx(bin, bin, MORPH_OPEN, k);
    threshold(bin, bin, 250, 255, THRESH_BINARY);
    return bin;
}

/* ========== 7) 노이즈/세로선 제거 ========== */
static Mat cleanNoiseAndLines(const Mat& bin){
    Mat cleaned = bin.clone();

    // 작은 점 노이즈 제거
    Mat k = getStructuringElement(MORPH_RECT, Size(2,2));
    morphologyEx(cleaned, cleaned, MORPH_OPEN, k);

    Mat labels, stats, centroids;
    int n = connectedComponentsWithStats(255-cleaned, labels, stats, centroids);
    for(int i=1;i<n;i++){
        int area = stats.at<int>(i, CC_STAT_AREA);
        if(area < 50){ // 너무 작은 블롭은 제거
            Mat mask = (labels==i);
            cleaned.setTo(255, mask);
        }
    }

    // 오른쪽 세로선 제거
    int cut = cleaned.cols * 0.03;
    Rect rightEdge(cleaned.cols - cut, 0, cut, cleaned.rows);
    cleaned(rightEdge).setTo(255);

    return cleaned;
}

/* ========== 8) 바깥 테두리 방지 ========== */
static vector<Point2f> shrinkQuad(const vector<Point2f>& q, float rate=0.015f){
    Point2f c(0,0); for(auto&p:q) c += p; c *= 0.25f;
    vector<Point2f> out; out.reserve(4);
    for(auto&p:q) out.push_back( c + (p - c) * (1.0f - rate) );
    return out;
}

/* ========== 9) main ========== */
int main(int argc, char** argv){
    if(argc<2){ cerr<<"Usage: ./ocr <image_path>\n"; return 1; }
    Mat img = imread(argv[1]); if(img.empty()){ cerr<<"이미지 로드 실패\n"; return 2; }

    undistortIfHaveCalib("camera.yml", img);

    const int targetW = 1200;
    double s = img.cols>targetW ? (double)targetW/img.cols : 1.0;
    Mat small; resize(img, small, Size(), s, s);

    vector<Point2f> quadS;
    if(!detectDocumentQuad(small, quadS) || quadS.size()!=4){
        cerr<<"문서 경계 감지 실패\n"; return 3;
    }
    vector<Point2f> quad; for(auto&p: quadS) quad.emplace_back(p.x/s, p.y/s);
    quad = shrinkQuad(quad, 0.015f);

    Mat upright = warpToA4(img, quad, 3508);
    Mat textWhite = makeTextOnWhite(upright);
    Mat finalClean = cleanNoiseAndLines(textWhite);

    vector<Point> qi; for(auto&p: quad) qi.emplace_back(cvRound(p.x), cvRound(p.y));
    for(int i=0;i<4;++i) line(img, qi[i], qi[(i+1)%4], Scalar(0,255,0), 3, LINE_AA);

    imwrite("detected_quad.jpg", img);
    imwrite("upright_a4.jpg", upright);
    imwrite("text_white.jpg", textWhite);
    imwrite("final_clean.jpg", finalClean);

    cout<<"저장됨: detected_quad.jpg, upright_a4.jpg, text_white.jpg, final_clean.jpg\n";
    return 0;
}
