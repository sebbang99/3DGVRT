import cv2
import numpy as np

# 이미지 불러오기
image = cv2.imread('input.png')
image = cv2.resize(image, (800, 472))
# image = cv2.resize(image, (1920, 1080))


# 양방향 필터 적용
filtered = cv2.bilateralFilter(image, 1, sigmaColor=50, sigmaSpace=10)
# filtered = cv2.medianBlur(image,3)
# 결과 저장
cv2.imwrite('filtered.jpg', filtered)

# 결과 보기
cv2.imshow('Original', image)
cv2.imshow('Bilateral Filtered', filtered)
cv2.waitKey(0)
cv2.destroyAllWindows()