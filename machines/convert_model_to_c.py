
def convert_to_c_array(filename):
    with open(filename, "rb") as f:
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

convert_to_c_array("model.tflite")
