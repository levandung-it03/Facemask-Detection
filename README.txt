1. Thực hiện xây dựng model và dataset với 2 class trên Teachable
2. Xuất model với option TensorFlow Lite > Quantized để đơn giản hoá mô hình và được sử dụng trong các dạng toán 8-bit (thay vì 32-bit với keras)
3. Sử dụng công cụ xxd (và python) để convert model.tflite > model.cc


    with open(filepath, "rb") as f:
        data = f.read()

    # Initialization (with C arr head)
    c_array = "const unsigned char model_data[] = {"

    # Convert each by of bites to hex.
    for i, byte in enumerate(data):
        if i % 12 == 0:
            c_array += "\n    "
        c_array += "0x{:02x}, ".format(byte)

    # Assign result, and tail
    c_array = c_array.rstrip(", ") + "\n};\n"

    # Add length information as needed
    c_array += "const unsigned int model_data_len = {};\n".format(len(data))

    with open("model_data.cc", "w") as f:
        f.write(c_array)
    print("model_data.cc has been created successfully")

4. Di chuyển file vào thư mục, đặt cùng cấp với file arduino IDE để thực hiện import và xử lý

5. C:\Users\<Your-Username>\AppData\Local\Arduino15\packages\esp32\hardware\esp32\<version>
esp32cam.upload.speed=