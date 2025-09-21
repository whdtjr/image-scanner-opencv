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

    static void onMouse(int event, int x, int y, int, void* userdata) {
        auto* app = reinterpret_cast<App*>(userdata);
        Point2f p((float)x,(float)y);

        if (event == EVENT_LBUTTONDOWN) {
            // 가까운 점 선택
            for (int i=0;i<4;i++){
                if (norm(app->quad[i]-p) <= HIT_R) { app->sel=i; break; }
            }
        } else if (event == EVENT_MOUSEMOVE) {
            if (app->sel>=0) {
                app->quad[app->sel] = p;
            }
        } else if (event == EVENT_LBUTTONUP) {
            app->sel = -1;
        }
    }
};

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

static void drawOverlay(const Mat& img, const array<Point2f,4>& q, int sel) {
    Mat vis = img.clone();
    // 엣지
    for (int i=0;i<4;i++) line(vis, q[i], q[(i+1)%4], COL_EDGE, 2, LINE_AA);
    // 점
    for (int i=0;i<4;i++){
        circle(vis, q[i], HIT_R, i==sel?COL_SEL:COL_PT, FILLED, LINE_AA);
        putText(vis, (i==0?"TL":i==1?"TR":i==2?"BR":"BL"),
                q[i]+Point2f(6,-6), FONT_HERSHEY_SIMPLEX, 0.5, Scalar(255,200,0), 1, LINE_AA);
    }
    imshow("Source (drag corners)", vis);
}

static void doWarpShow(const App& app) {
    // 대상 사각형(직사각형)
    array<Point2f,4> dst = {
        Point2f(0,0), Point2f((float)app.outW-1,0),
        Point2f((float)app.outW-1,(float)app.outH-1), Point2f(0,(float)app.outH-1)
    };
    Mat H = getPerspectiveTransform(app.quad.data(), dst.data());
    Mat warped;
    warpPerspective(app.img, warped, H, Size(app.outW, app.outH), INTER_CUBIC, BORDER_REPLICATE);
    imshow("Warped (W/H trackbars, s=save)", warped);
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

    namedWindow("Source (drag corners)", WINDOW_AUTOSIZE);
    setMouseCallback("Source (drag corners)", App::onMouse, &app);

    namedWindow("Warped (W/H trackbars, s=save)", WINDOW_AUTOSIZE);
    // 트랙바로 W/H 조정
    createTrackbar("W", "Warped (W/H trackbars, s=save)", &app.outW, 4000);
    createTrackbar("H", "Warped (W/H trackbars, s=save)", &app.outH, 4000);

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
        drawOverlay(app.img, app.quad, app.sel);

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
            std::cout << "Saved: warped.png ("<<app.outW<<"x"<<app.outH<<")\n";
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
