#include <opencv2/opencv.hpp>
#include <array>
#include <iostream>
using namespace cv;
using std::array;

static const int HIT_R = 10;        // 점 잡기 반경(px)
static const Scalar COL_PT(0,200,255), COL_EDGE(0,255,0), COL_SEL(0,0,255);

struct App {
    Mat img;                    // 원본
    array<Point2f,4> quad;      // TL, TR, BR, BL (인덱스 고정)
    int sel = -1;               // 선택중인 포인트 인덱스
    int outW, outH;             // 출력 해상도
    bool keepAspect = false;    // 종횡비 고정 여부(단축키 a)
    int srcW = 800;
    int srcH = 600;
    int warpW = 800;
    int warpH = 600; // A4 비율 대충
    double srcScale = 1.0;
    Point2f srcOffset = Point2f(0,0);
    bool doProc = true;   // 처리 on/off (트랙바)
    int medianK = 1;      // 노이즈 제거용 median 커널(홀수, >=1)
    int block   = 32;     // adaptive threshold block size(홀수, >=3)
    int C       = 18;     // adaptive threshold C (실제는 C - 15로 사용하여 음수 가능)

    static void onMouse(int event, int x, int y, int, void* userdata) 
    {
        auto* app = reinterpret_cast<App*>(userdata);

        // 창(캔버스) 좌표 → 축소 이미지 좌표
        Point2f p_disp = Point2f((float)x, (float)y) - app->srcOffset;

        // 축소된 이미지 영역 밖 클릭은 무시
        if (p_disp.x < 0 || p_disp.y < 0) return;
        if (p_disp.x > app->img.cols * app->srcScale ||
            p_disp.y > app->img.rows * app->srcScale) return;

        // 축소 이미지 좌표 → 원본 좌표
        Point2f p = p_disp * (1.0f / (float)app->srcScale);

        if (event == EVENT_LBUTTONDOWN) {
            for (int i=0;i<4;i++){
                if (norm(app->quad[i]-p) <= HIT_R) { app->sel=i; break; }
            }
        } else if (event == EVENT_MOUSEMOVE) {
            if (app->sel>=0) app->quad[app->sel] = p;
        } else if (event == EVENT_LBUTTONUP) {
            app->sel = -1;
        }
    }
};

static Mat preprocessDoc(const Mat& warpedColor, const App& app) {
    // 1) 그레이
    Mat gray; cvtColor(warpedColor, gray, COLOR_BGR2GRAY);

    // 2) 노이즈 제거 (median blur)
    int k = std::max(1, app.medianK | 1); // 홀수 보장
    if (k > 1) medianBlur(gray, gray, k);

    // 3) 이진화 (Adaptive Gaussian)
    int block = std::max(3, app.block | 1); // 홀수 보장
    int C = app.C - 15; // 트랙바 0..30 → 실제 -15..+15
    Mat bin;
    adaptiveThreshold(gray, bin, 255,
                      ADAPTIVE_THRESH_GAUSSIAN_C, THRESH_BINARY,
                      block, C);
    return bin;
}

// TL,TR,BR,BL 순으로 대략 정렬하는 유틸(옵션: 키 'o'로 실행)
static array<Point2f,4> orderCorners(array<Point2f,4> pts) {
    // 임의 순서의 4점을 받아 TL,TR,BR,BL 순서로 정렬
    // 여기선 간단히: y(상->하), x(좌->우)로 두 그룹 나눠 정렬
    std::vector<Point2f> v(pts.begin(), pts.end());
    std::sort(v.begin(), v.end(), [](const Point2f& a, const Point2f& b){
        if (fabs(a.y-b.y) > 1e-3) return a.y < b.y;
        return a.x < b.x;
    });
    array<Point2f,4> out;
    if (v[0].x < v[1].x) { out[0]=v[0]; out[1]=v[1]; } // top: TL,TR
    else                 { out[0]=v[1]; out[1]=v[0]; }
    if (v[2].x < v[3].x) { out[3]=v[2]; out[2]=v[3]; } // bottom: BL,BR
    else                 { out[3]=v[3]; out[2]=v[2]; }
    return out;
}

static void drawOverlay(const App& app) {
    Mat vis = app.img.clone();

    // 원본 좌표계에 오버레이 그리기
    for (int i=0;i<4;i++) line(vis, app.quad[i], app.quad[(i+1)%4], COL_EDGE, 2, LINE_AA);
    for (int i=0;i<4;i++){
        circle(vis, app.quad[i], HIT_R, i==app.sel?COL_SEL:COL_PT, FILLED, LINE_AA);
        putText(vis, (i==0?"TL":i==1?"TR":i==2?"BR":"BL"),
                app.quad[i]+Point2f(6,-6), FONT_HERSHEY_SIMPLEX, 0.5, Scalar(255,200,0), 1, LINE_AA);
    }

    // 고정 크기 캔버스 생성
    Mat canvas(app.srcH, app.srcW, vis.type(), Scalar(30,30,30));

    // 창 크기에 맞춰 스케일 계산 (비율 유지)
    double s = std::min((double)app.srcW / vis.cols, (double)app.srcH / vis.rows);
    if (s > 1.0) s = 1.0; // 원본보다 키우지 않음(원하면 이 줄 삭제)

    // 축소 및 중앙 정렬
    Mat small; resize(vis, small, Size(), s, s, INTER_AREA);
    int ox = (app.srcW - small.cols) / 2;
    int oy = (app.srcH - small.rows) / 2;
    small.copyTo(canvas(Rect(ox, oy, small.cols, small.rows)));

    // 마우스 보정용 파라미터 갱신
    const_cast<App&>(app).srcScale  = s;
    const_cast<App&>(app).srcOffset = Point2f((float)ox, (float)oy);

    imshow("Source (drag corners)", canvas);
}

static void doWarpShow(const App& app) {
    array<Point2f,4> dst = {
        Point2f(0,0), Point2f((float)app.outW-1,0),
        Point2f((float)app.outW-1,(float)app.outH-1), Point2f(0,(float)app.outH-1)
    };
    Mat H = getPerspectiveTransform(app.quad.data(), dst.data());
    Mat warped;
    warpPerspective(app.img, warped, H, Size(app.outW, app.outH), INTER_CUBIC, BORDER_REPLICATE);

    Mat toShow = app.doProc ? preprocessDoc(warped, app) : warped;
    // 고정 크기 캔버스
    Mat canvas(app.warpH, app.warpW, toShow.type() == CV_8U ? CV_8UC1 : toShow.type(), Scalar(30,30,30));
    double s = std::min((double)app.warpW / toShow.cols, (double)app.warpH / toShow.rows);
    if (s > 1.0) s = 1.0;

    Mat small; resize(toShow, small, Size(), s, s, INTER_AREA);
    int ox = (app.warpW - small.cols) / 2;
    int oy = (app.warpH - small.rows) / 2;

    if (small.channels()==1) {
        // 캔버스가 1채널이면 바로 복사
        small.copyTo(canvas(Rect(ox, oy, small.cols, small.rows)));
    } else {
        small.copyTo(canvas(Rect(ox, oy, small.cols, small.rows)));
    }

    imshow("Warped (W/H trackbars, s=save)", canvas);
}

int main(int argc, char** argv){
    if(argc < 2){
        std::cout << "Usage: drag_warp image.jpg [outW outH]\n";
        return 0;
    }
    App app;
    app.img = imread(argv[1], IMREAD_COLOR);
    if(app.img.empty()){ std::cerr << "Failed to read: " << argv[1] << "\n"; return -1; }

    // 초기 쿼드: 이미지 안쪽 여백 10%
    float mX = app.img.cols * 0.1f, mY = app.img.rows * 0.1f;
    app.quad = { Point2f(mX,mY), Point2f(app.img.cols-mX,mY),
                 Point2f(app.img.cols-mX, app.img.rows-mY), Point2f(mX, app.img.rows-mY) };

    // 출력 크기 초기값
    app.outW = (argc>=3)? std::max(50, atoi(argv[2])) : 800;
    app.outH = (argc>=4)? std::max(50, atoi(argv[3])) : (int)(app.outW * 1.4142 /*A4 비율 대충*/);


    namedWindow("Source (drag corners)", WINDOW_NORMAL | WINDOW_KEEPRATIO);
    setMouseCallback("Source (drag corners)", App::onMouse, &app);
    resizeWindow("Source (drag corners)", app.srcW, app.srcH);

    namedWindow("Warped (W/H trackbars, s=save)", WINDOW_NORMAL | WINDOW_KEEPRATIO);
    resizeWindow("Warped (W/H trackbars, s=save)", app.srcW, app.srcH);
    // 트랙바로 W/H 조정
    createTrackbar("W", "Warped (W/H trackbars, s=save)", &app.outW, 4000);
    createTrackbar("H", "Warped (W/H trackbars, s=save)", &app.outH, 4000);
    // createTrackbar("proc(0/1)", "Warped (W/H trackbars, s=save)", (int*)&app.doProc, 1);
    // createTrackbar("medianK",   "Warped (W/H trackbars, s=save)", &app.medianK, 15); // 1~15 (홀수로 사용)
    // createTrackbar("block",     "Warped (W/H trackbars, s=save)", &app.block,   101); // 3~101 (홀수로 사용)
    // createTrackbar("C(+/-15)",  "Warped (W/H trackbars, s=save)", &app.C,       30);  // 실제 -15..+15
    std::cout <<
      "[조작]\n"
      " - 마우스: 점 드래그 (TL,TR,BR,BL 인덱스 고정)\n"
      " - 키 o : 점을 TL,TR,BR,BL 순서로 자동 정렬\n"
      " - 키 a : 종횡비 고정 토글 (W/H를 연동)\n"
      " - 키 +/- 또는 화살표: W,H 미세조정\n"
      " - 키 s : warped.png 저장\n"
      " - 키 r : 쿼드 초기화\n"
      " - ESC  : 종료\n";

    double aspect = (double)app.outW / std::max(1,app.outH);
    for(;;){
        drawOverlay(app);

        // 종횡비 고정 옵션
        if(app.keepAspect){
            int trackW = getTrackbarPos("W","Warped (W/H trackbars, s=save)");
            if(trackW != app.outW){
                app.outW = std::max(50, trackW);
                app.outH = std::max(50, (int)(app.outW / aspect));
                setTrackbarPos("H","Warped (W/H trackbars, s=save)", app.outH);
            }
            int trackH = getTrackbarPos("H","Warped (W/H trackbars, s=save)");
            if(trackH != app.outH){
                app.outH = std::max(50, trackH);
                app.outW = std::max(50, (int)(app.outH * aspect));
                setTrackbarPos("W","Warped (W/H trackbars, s=save)", app.outW);
            }
        } else {
            app.outW = std::max(50, getTrackbarPos("W","Warped (W/H trackbars, s=save)"));
            app.outH = std::max(50, getTrackbarPos("H","Warped (W/H trackbars, s=save)"));
            aspect = (double)app.outW / std::max(1,app.outH);
        }

        doWarpShow(app);

        int key = waitKey(16);
        if(key == 27) break;                    // ESC
        else if(key=='s' || key=='S'){          // 저장
            array<Point2f,4> dst = {
                Point2f(0,0), Point2f((float)app.outW-1,0),
                Point2f((float)app.outW-1,(float)app.outH-1), Point2f(0,(float)app.outH-1)
            };
            Mat H = getPerspectiveTransform(app.quad.data(), dst.data());
            Mat warped; warpPerspective(app.img, warped, H, Size(app.outW, app.outH), INTER_CUBIC, BORDER_REPLICATE);
            imwrite("warped.png", warped);

            Mat warped_bin = preprocessDoc(warped, app);
            imwrite("final_clean.jpg", warped_bin);

            std::cout << "Saved: warped.png ("<<app.outW<<"x"<<app.outH<<")\n";
            std::cout << "Saved: final_clean.jpg ("<<warped_bin.cols<<"x"<<warped_bin.rows<<")\n";
        } else if(key=='o' || key=='O'){        // 자동 정렬
            app.quad = orderCorners(app.quad);
        } else if(key=='a' || key=='A'){        // 종횡비 고정 토글
            app.keepAspect = !app.keepAspect;
            std::cout << "keepAspect = " << (app.keepAspect?"ON":"OFF") << "\n";
        } else if(key=='+'){                    // 크기 키보드 조정
            app.outW = std::min(4000, app.outW+20);
            app.outH = std::min(4000, app.outH+20);
            setTrackbarPos("W","Warped (W/H trackbars, s=save)", app.outW);
            setTrackbarPos("H","Warped (W/H trackbars, s=save)", app.outH);
        } else if(key=='-'){
            app.outW = std::max(50, app.outW-20);
            app.outH = std::max(50, app.outH-20);
            setTrackbarPos("W","Warped (W/H trackbars, s=save)", app.outW);
            setTrackbarPos("H","Warped (W/H trackbars, s=save)", app.outH);
        } else if(key == 81 /*←*/){
            app.outW = std::max(50, app.outW-10);
            setTrackbarPos("W","Warped (W/H trackbars, s=save)", app.outW);
        } else if(key == 83 /*→*/){
            app.outW = std::min(4000, app.outW+10);
            setTrackbarPos("W","Warped (W/H trackbars, s=save)", app.outW);
        } else if(key == 82 /*↑*/){
            app.outH = std::min(4000, app.outH+10);
            setTrackbarPos("H","Warped (W/H trackbars, s=save)", app.outH);
        } else if(key == 84 /*↓*/){
            app.outH = std::max(50, app.outH-10);
            setTrackbarPos("H","Warped (W/H trackbars, s=save)", app.outH);
        } else if(key=='r' || key=='R'){
            float mX = app.img.cols * 0.1f, mY = app.img.rows * 0.1f;
            app.quad = { Point2f(mX,mY), Point2f(app.img.cols-mX,mY),
                         Point2f(app.img.cols-mX, app.img.rows-mY), Point2f(mX, app.img.rows-mY) };
        }
    }
    destroyAllWindows();
    return 0;
}
