## 빌드 및 실행
```bash
# 빌드
$ g++ ocr.cpp -o ocr $(pkg-config --cflags --libs opencv4)

# 실행
$ ./ocr [이미지경로]
```
