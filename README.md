# ⌚ T-Watch S3 Binary Clock (LVGL + Gesture Control)

Hiển thị thời gian trên **LilyGo T-Watch S3 (ESP32-S3)** với giao diện tối giản và điều khiển hoàn toàn bằng cảm ứng (gesture).

---

## 📷 Preview

### 🧮 Chế độ nhị phân (Binary mode)
![Binary Mode](img/img1.jpg)

### 🔢 Chế độ thập phân (Digital mode)
![Digital Mode](img/img2.jpg)

### ⚙️ Chế độ chỉnh cài đặt (Setting mode)
![Setting Mode](img/img3.jpg)

---

## ⚙️ Tính năng

- 2 chế độ hiển thị:
  - Binary clock (bit)
  - Digital clock (ngày + giờ)
- Gesture điều khiển:
  - Vuốt lên → tăng giá trị
  - Vuốt xuống → giảm giá trị
  - Double tap → chuyển field (day → month → year → ...)
  - Giữ 5 giây → vào / thoát chế độ chỉnh giờ
- Chế độ Attack / Deauth UI mới:
  - Giữ 10 giây → vào chế độ Attack
  - Scan Wi-Fi xung quanh và hiển thị danh sách AP
  - Chọn các AP bằng checkbox
  - Nhấn Attack để bắt đầu gửi gói tin deauth/attack
  - Hiển thị thông tin chi tiết trong quá trình attack:
    - SSID
    - BSSID
    - Channel
    - RSSI
    - Số packet đã gửi
  - Có thể dừng quá trình attack bằng nút Stop
- Tự động:
  - Tắt màn hình khi không hoạt động
  - Bật màn hình khi:
    - Nghiêng cổ tay (BMA423)
    - Double tap

---

## 🧠 Logic điều khiển

| Hành động        | Chức năng |
|-----------------|----------|
| Giữ 5 giây       | Vào/thoát setting mode |
| Vuốt lên         | Tăng giá trị |
| Vuốt xuống       | Giảm giá trị |
| Double tap       | Đổi field |
| Không thao tác   | Tắt màn hình |

---

## �️ Hướng dẫn sử dụng chế độ Attack

1. Giữ nút chạm trên màn hình khoảng 10 giây để vào chế độ Attack.
2. Nhấn nút Scan để quét các AP Wi-Fi gần đó.
3. Sau khi có danh sách, chọn các AP bằng checkbox.
4. Nhấn Attack để bắt đầu quá trình gửi gói tin deauth/attack.
5. Trong lúc chạy, màn hình sẽ hiển thị:
   - SSID hiện tại
   - BSSID
   - Kênh Wi-Fi
   - RSSI
   - Số packet đã gửi
6. Nhấn Stop để dừng quá trình attack bất cứ lúc nào.

> Lưu ý: chức năng này chỉ mang ý nghĩa demo/khảo sát trên thiết bị của bạn và nên được sử dụng phù hợp với quy định pháp luật và chính sách an toàn mạng.

---

## �🔧 Build (PlatformIO)

```bash
python -m esptool --chip esp32s3 --baud 460800 write-flash 0x0 merged.bin
or
pio run -e twatchs3 -t upload