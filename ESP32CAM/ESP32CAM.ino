// #include "soc/soc.h"
// #include "soc/rtc_cntl_reg.h"
// #include "esp_http_server.h"
// #include "esp_timer.h"
// #include "img_converters.h"

#include <esp32-hal-ledc.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include "model_data.cc"
#include <tensorflow/lite/micro/micro_interpreter.h>
#include <tensorflow/lite/micro/micro_mutable_op_resolver.h>
#include <tensorflow/lite/schema/schema_generated.h>
#include "secrets.h"
#include "esp_camera.h"
#include "fb_gfx.h"
#include "fd_forward.h"
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>

// Global varibales
WiFiClient client;
UniversalTelegramBot bot(BOT_TOKEN, client);
WebSocketsClient webSocket;
bool flashState = false;
bool sendPhoto = true;
const int kImageWidth = 96;
const int kImageHeight = 96;
const int kImageChannels = 3; // As RGB
constexpr int tensor_arena_size = 40 * 1024;  // Adjust based on the model’s memory needs
tflite::MicroInterpreter* interpreter;

void setupCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = 5;
  config.pin_d1 = 18;
  config.pin_d2 = 19;
  config.pin_d3 = 21;
  config.pin_d4 = 36;
  config.pin_d5 = 39;
  config.pin_d6 = 34;
  config.pin_d7 = 35;
  config.pin_xclk = 0;
  config.pin_pclk = 22;
  config.pin_vsync = 25;
  config.pin_href = 23;
  config.pin_sccb_sda = 26;
  config.pin_sccb_scl = 27;
  config.pin_reset = -1;
  config.pin_pwdn = 32;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // Kích thước ảnh (giảm kích thước nếu cần)
  config.frame_size = FRAMESIZE_QVGA;
  config.pixel_format = PIXFORMAT_JPEG; // Set pixel format

  // Khởi tạo camera
  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("Failed to initialize camera");
    return;
  }
}

// void continiouslyListenAndResponseTelegram() {
//   Serial.print("Handle Post New Message: ");
//   Serial.println(totalMessages);

//   int totalMessages = bot.getUpdates(bot.last_message_received + 1);

//   for (int i = 0; i < totalMessages; i++) {
//     String chatId = String(bot.messages[i].chat_id);
//     if (chatId != CHAT_ID) {
//       bot.sendMessage(chatId, "Unauthorized User", "");
//       continue;
//     }

//     String text = bot.messages[i].text;
//     Serial.println(text);

//     String fromName = bot.messages[i].from_name;
//     if (text == "/start") {
//       String welcome = "Welcome , " + fromName + "\n";
//       welcome += "Use the following commands to interact with the ESP32-CAM \n";
//       welcome += "/photo : takes a new photo\n";
//       welcome += "/flash : toggles flash LED \n";
//       bot.sendMessage(CHAT_ID, welcome, "");
//     }
//     if (text == "/flash") {
//       flashState = !flashState;
//       figitalWrite(4, flashState);
//       Serial.println("Change flash LED state");
//       bot.sendMessage(chatId, "Flash is " + String(flashState), "");
//     }
//     if (text == "/photo") {
//       sendPhoto = !sendPhoto;
//       String msg = (sendPhoto ? "Start" : "Stop") + " receiving photos";
//       Serial.println(msg);
//       bot.sendMessage(chatId, msg, "");
//     }
//   }
// }

// void sendPredictedPhotoTelegram(camera_fb_t* fb, String maskStatusAsMsg) {
//   Serial.println("Connecting to Telegram-" + String(TELEGRAM_DOMAIN));

//   // Sending data to telegram with TCP network method (as default setting on telegram app)
//   if (clientTCP.connect(TELEGRAM_DOMAIN, 443)) {
//     Serial.println("Connect to Telegram successfully");

//     uint16_t imageLen = fb->len;
//     if (imageLen != 0) {
//       // Form data boundaries
//       String boundary = "--RandomNerdTutorials\r\n";
//       String msg = boundary + "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n" + String(CHAT_ID) + "\r\n";
//       msg += boundary + "Content-Disposition: form-data; name=\"photo\"; filename=\"image.jpg\"\r\n";
//       msg += "Content-Type: image/jpeg\r\n\r\n";

//       uint16_t extraLen = msg.length();
//       uint16_t totalLen = imageLen + extraLen + 4;  // Additional for ending boundary

//       clientTCP.println("POST /bot" + String(BOT_TOKEN) + "/sendPhoto HTTP/1.1");
//       clientTCP.println("Host: " + String(TELEGRAM_DOMAIN));
//       clientTCP.println("Content-Length: " + String(totalLen));
//       clientTCP.println("Content-Type: multipart/form-data; boundary=RandomNerdTutorials");
//       clientTCP.println();

//       // Send form data
//       clientTCP.print(msg);  // Send chat_id and the photo field name
//       clientTCP.write(fb->buf, fb->len);  // Send image data (frame buffer)

//       clientTCP.print("\r\n--RandomNerdTutorials--\r\n"); // Closing boundary
//       Serial.println("Image sent to Telegram");
//     }

//     if (maskStatusAsMsg.length() > 0) {
//       clientTCP.print("\r\n--RandomNerdTutorials\r\n");
//       clientTCP.print("Content-Disposition: form-data; name=\"caption\"\r\n\r\n");
//       clientTCP.print(maskStatusAsMsg);  // Send the mask status message as caption
//       clientTCP.print("\r\n--RandomNerdTutorials--\r\n"); // Closing boundary
//       Serial.println("Mask Status sent to Telegram");
//     }
//   } else {
//     Serial.println("Connection Failed!");
//   }
// }

String predictMaskDetection(camera_fb_t* fb) {
  // Give input image to model
  memcpy(interpreter->input(0)->data.uint8, fb->buf, kImageWidth * kImageHeight * kImageChannels);

  // Predict class by input image (result as mask or not)
  interpreter->Invoke();
  
  // Get result
  uint8_t* output = interpreter->output(0)->data.uint8;

  Serial.print(output[0]);
  Serial.print(" is Has - No mask: ");
  Serial.println(output[1]);
  if (output[0] > output[1]) {
    return "Has Mask";
  } else {
    return "No Mask";
  }
}

void setupTensorFlow() {
  tflite::MicroMutableOpResolver<5> resolver;  // Adjust the number based on your model's needs
  resolver.AddConv2D();
  resolver.AddMaxPool2D();
  resolver.AddFullyConnected();
  resolver.AddSoftmax();
  resolver.AddReshape();

  uint8_t tensor_arena[tensor_arena_size];
  
  // Load model
  const tflite::Model* model = tflite::GetModel(model_data);

  // Training model
  interpreter = new tflite::MicroInterpreter(model, resolver, tensor_arena, tensor_arena_size);

  interpreter->AllocateTensors();
  Serial.println("Model loaded and tensors allocated.");
}

// void logsOfWebSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
//   if (type == WStype_CONNECTED) {
//     Serial.println("Connected to WebSocket server");
//   } else if (type == WStype_DISCONNECTED) {
//     Serial.println("Disconnected from WebSocket server");
//   }
// }

void setup() {
  Serial.begin(115200);
  WiFi.begin(SECRET_WIFI_NAME, SECRET_WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");

  setupCamera();
  setupTensorFlow();

  // webSocket.begin(String(FASTAPI_WEBSOCKET_IP), 8000, "/ws/mask-detection");
  // webSocket.onEvent(logsOfWebSocketEvent);
}

void loop() {
  // continiouslyListenAndResponseTelegram();

  if (sendPhoto) {
    camera_fb_t* fb = NULL;
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      delay(1000);
      ESP.restart();
      return;
    }
    Serial.println("Connected");

    if (fb->width != kImageWidth || fb->height != kImageHeight) {
      Serial.println("Wrong size's input image");
      esp_camera_fb_return(fb);
      return;
    }

    String result = predictMaskDetection(fb);
    Serial.println(result);    
    // Send to Telegram
  }

  delay(3000);
}
