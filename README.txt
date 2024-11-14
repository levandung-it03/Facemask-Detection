1. Setup:
- Setup ESP32 Camera
- Setup WiFiSecure: config ESP32 Access Point - Station (AP_STA) + config telegram certificate
- Setup ESP32 Camera Server

2. Loop:
- Gửi ảnh gốc
- While: liên tục lấy response cuối từ telegram
- Kiểm tra option từ telegram

3. cmd_handler
- 2 loại input:
	+ {var: val}: các option cơ bản (trên giao diện website) như quality, brightness,...	==> Chỉ cần 2 tham số
	+ "myCmd": các option nâng cao như đọc địa chỉ mac, restart, bật tắt flash,...		==> Cần hơn 3 tham số

4. stream_handler
- Nhận một stream request từ Client.
- Thực hiện lấy ảnh theo yêu cầu bằng ESP32-Cam thông qua con trỏ fb.
- Xử lý lại ảnh để phù hợp với luồng hiện tại (size ảnh, dạng tệp,...)
- Trả về ảnh cho client, dọn dẹp bộ nhớ đệm ESP32-Cam.
- Luồng luôn hoạt động trong hàm while(true)
- Nếu có lỗi xảy ra thì dừng vòng while

