## 사용 방법 
```bash
cmake -S . -B build -DOpenCV_DIR=/usr/lib/x86_64-linux-gnu/cmake/opencv4

bear --output compile_commands.json -- cmake --build build -j # 만약 opencv code intellisense 필요 시 bear로 추적 아니라면 제거 가능
```

## 결과 
### 원본 이미지
![Image](https://github.com/user-attachments/assets/bd9d655a-7d43-41c9-8b78-811da2a8211d)
### 스캔한 이미지
![Image](https://github.com/user-attachments/assets/0767c634-7c16-486f-87bf-3eda133e7405)
